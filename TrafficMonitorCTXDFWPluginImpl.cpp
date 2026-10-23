#include "TrafficMonitorCTXDFWPluginImpl.h"

///////////////////////////////////////////////////////////////////////////////
#include <commdlg.h>    // ChooseColor
#include <cstdio>       // swprintf_s
#include <cstdlib>      // _countof
#include <cwchar>       // wcstoul / _wtoi

// 设置对话框控件 ID
#define IDC_S_CHOOSECOLOR  2001
#define IDC_S_PREVIEW      2002
#define IDC_S_AUTOFIX      2003
#define IDC_S_INTERVAL     2004
#define IDC_S_APPLY        2005
#define IDC_S_FONT         2006
#define IDC_S_BOLD         2007
#define IDC_S_SIZE         2008
#define IDC_S_PREVIEW2     2009

// 设置对话框的工作数据（Apply 前暂存，Cancel 可丢弃）
struct DlgData {
    COLORREF color;
    int     interval;
    bool    autoFix;
    HBRUSH  brush;
    HFONT   font;        // 设置界面统一字体（微软雅黑），对话框销毁时释放
    std::wstring fontName;
    bool    bold;
    int     fontSize;
    HFONT   iconFont;    // “虚”字预览字体，WM_DESTROY 释放
};

// 解析 RRGGBB 十六进制颜色；失败返回默认值
static COLORREF ParseHexColor(const wchar_t* s, COLORREF def) {
    if (!s || !*s) return def;
    unsigned long v = wcstoul(s, nullptr, 16);
    BYTE r = (BYTE)((v >> 16) & 0xFF);
    BYTE g = (BYTE)((v >> 8) & 0xFF);
    BYTE b = (BYTE)(v & 0xFF);
    return RGB(r, g, b);
}

// 将当前对话框设置写回插件并落盘（确认 / 应用 共用）
static void ApplySettings(HWND hDlg, DlgData* dlg) {
    wchar_t buf[16] = {0};
    GetWindowTextW(GetDlgItem(hDlg, IDC_S_INTERVAL), buf, 16);
    int iv = _wtoi(buf);
    if (iv < 5)    iv = 5;       // 下限保护，避免过于频繁
    if (iv > 3600) iv = 3600;    // 上限保护

    bool af = (IsDlgButtonChecked(hDlg, IDC_S_AUTOFIX) == BST_CHECKED);

    g_plugin.SetIconColor(dlg->color);
    g_plugin.SetCheckInterval(iv);
    g_plugin.SetAutoFix(af);     // SetAutoFix 内部已 SaveConfig

    // “虚”字外观：字体名 / 粗体 / 字号
    wchar_t nm[64]; GetWindowTextW(GetDlgItem(hDlg, IDC_S_FONT), nm, _countof(nm));
    bool bold = (IsDlgButtonChecked(hDlg, IDC_S_BOLD) == BST_CHECKED);
    wchar_t sz[8];  GetWindowTextW(GetDlgItem(hDlg, IDC_S_SIZE), sz, _countof(sz));
    int size = _wtoi(sz);
    if (size < 6)  size = 6;
    if (size > 72) size = 72;
    g_plugin.SetIconFontName(nm);
    g_plugin.SetIconFontBold(bold);
    g_plugin.SetIconFontSize(size);

    g_plugin.SaveConfig();
    g_plugin.RefreshTooltip();   // 配置立即生效：刷新 Tooltip 文本
    g_plugin.MarkOptionChanged();
}

// 给每个子控件统一设置字体（WM_SETFONT）
static BOOL CALLBACK SetChildFont(HWND h, LPARAM lp) {
    SendMessageW(h, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

// 按“虚”字设置创建字体（调用方负责 DeleteObject）
static HFONT MakeIconFont(const std::wstring& name, bool bold, int size) {
    return CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       name.empty() ? L"Microsoft YaHei" : name.c_str());
}

// 常用字号（下拉列表）
static const int kIconSizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48};

// 枚举系统字体填充“虚”字字体下拉框（去重，跳过 @ 竖向变体）
struct FontEnumCtx {
    HWND        hCombo;
    std::wstring cur;
    int         sel;
};
static int CALLBACK EnumFontProc(const LOGFONTW* lf, const TEXTMETRICW*,
                                 DWORD, LPARAM lp) {
    FontEnumCtx* ctx = (FontEnumCtx*)lp;
    if (lf->lfFaceName[0] == L'@') return 1;
    if (SendMessageW(ctx->hCombo, CB_FINDSTRINGEXACT, -1,
                     (LPARAM)lf->lfFaceName) != CB_ERR)
        return 1;                                  // 已存在，去重
    int idx = (int)SendMessageW(ctx->hCombo, CB_ADDSTRING, 0,
                                (LPARAM)lf->lfFaceName);
    if (ctx->cur == lf->lfFaceName) ctx->sel = idx;
    return 1;
}

// 依据当前控件重建“虚”字预览字体并重绘预览
static void UpdateIconPreviewFont(HWND hDlg, DlgData* dlg) {
    wchar_t nm[64]; GetWindowTextW(GetDlgItem(hDlg, IDC_S_FONT), nm, _countof(nm));
    bool bold = (IsDlgButtonChecked(hDlg, IDC_S_BOLD) == BST_CHECKED);
    wchar_t sz[8];  GetWindowTextW(GetDlgItem(hDlg, IDC_S_SIZE), sz, _countof(sz));
    int size = _wtoi(sz);
    if (size < 6)  size = 6;
    if (size > 72) size = 72;
    dlg->fontName = nm;
    dlg->bold     = bold;
    dlg->fontSize = size;
    if (dlg->iconFont) DeleteObject(dlg->iconFont);
    dlg->iconFont = MakeIconFont(dlg->fontName, dlg->bold, dlg->fontSize);
    InvalidateRect(GetDlgItem(hDlg, IDC_S_PREVIEW2), nullptr, TRUE);
}

// 创建设置对话框的所有子控件并初始化
static void BuildSettingsControls(HWND hDlg) {
    HINSTANCE hInst = GetModuleHandle(nullptr);

    DlgData* dlg = new DlgData{};
    dlg->color    = g_plugin.GetIconColor();
    dlg->interval = g_plugin.GetCheckInterval();
    dlg->autoFix  = g_plugin.IsAutoFix();
    dlg->brush    = CreateSolidBrush(dlg->color);
    dlg->fontName = g_plugin.GetIconFontName();
    dlg->bold     = g_plugin.GetIconFontBold();
    dlg->fontSize = g_plugin.GetIconFontSize();
    dlg->iconFont = MakeIconFont(dlg->fontName, dlg->bold, dlg->fontSize);
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)dlg);

    CreateWindowExW(0, L"STATIC", L"图标颜色：",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 22, 80, 22, hDlg, nullptr, hInst, nullptr);
    CreateWindowExW(0, L"STATIC", L"  ",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_SUNKEN, 100, 20, 42, 22, hDlg,
        (HMENU)(INT_PTR)IDC_S_PREVIEW, hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"选择颜色...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 150, 19, 95, 24, hDlg,
        (HMENU)(INT_PTR)IDC_S_CHOOSECOLOR, hInst, nullptr);

    CreateWindowExW(0, L"BUTTON", L"自动修复（守护）",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 15, 60, 200, 22, hDlg,
        (HMENU)(INT_PTR)IDC_S_AUTOFIX, hInst, nullptr);

    CreateWindowExW(0, L"STATIC", L"检查间隔(秒)：",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 94, 90, 22, hDlg, nullptr, hInst, nullptr);
    wchar_t iv[16];
    swprintf_s(iv, _countof(iv), L"%d", dlg->interval);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", iv,
        WS_CHILD | WS_VISIBLE | ES_NUMBER, 110, 92, 60, 22, hDlg,
        (HMENU)(INT_PTR)IDC_S_INTERVAL, hInst, nullptr);

    // —— “虚”字外观设置 ——
    CreateWindowExW(0, L"STATIC", L"虚字字体：",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 126, 66, 22, hDlg, nullptr, hInst, nullptr);
    CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        86, 124, 120, 200, hDlg, (HMENU)(INT_PTR)IDC_S_FONT, hInst, nullptr);
    HWND hFontCb = GetDlgItem(hDlg, IDC_S_FONT);
    FontEnumCtx fctx{hFontCb, std::wstring(dlg->fontName), 0};
    HDC dc = GetDC(hDlg);
    LOGFONTW lf{}; lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, EnumFontProc, (LPARAM)&fctx, 0);
    ReleaseDC(hDlg, dc);
    SendMessageW(hFontCb, CB_SETCURSEL, fctx.sel, 0);

    CreateWindowExW(0, L"BUTTON", L"粗体",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 214, 126, 60, 22, hDlg,
        (HMENU)(INT_PTR)IDC_S_BOLD, hInst, nullptr);
    if (dlg->bold) CheckDlgButton(hDlg, IDC_S_BOLD, BST_CHECKED);

    CreateWindowExW(0, L"STATIC", L"字号：",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 158, 44, 22, hDlg, nullptr, hInst, nullptr);
    CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        62, 156, 56, 200, hDlg, (HMENU)(INT_PTR)IDC_S_SIZE, hInst, nullptr);
    HWND hSizeCb = GetDlgItem(hDlg, IDC_S_SIZE);
    int sizeSel = 0;
    for (int i = 0; i < (int)_countof(kIconSizes); ++i) {
        wchar_t s[8]; swprintf_s(s, _countof(s), L"%d", kIconSizes[i]);
        int idx = (int)SendMessageW(hSizeCb, CB_ADDSTRING, 0, (LPARAM)s);
        if (kIconSizes[i] == dlg->fontSize) sizeSel = idx;
    }
    SendMessageW(hSizeCb, CB_SETCURSEL, sizeSel, 0);

    // “虚”字预览（所见即所得：字体 + 粗体 + 字号 + 颜色）
    CreateWindowExW(0, L"STATIC", L"虚",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_SUNKEN, 150, 154, 54, 28, hDlg,
        (HMENU)(INT_PTR)IDC_S_PREVIEW2, hInst, nullptr);

    // —— 底部按钮 ——
    CreateWindowExW(0, L"BUTTON", L"应用",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 50, 210, 70, 26, hDlg,
        (HMENU)(INT_PTR)IDC_S_APPLY, hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"确认",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 140, 210, 70, 26, hDlg,
        (HMENU)(INT_PTR)IDOK, hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 230, 210, 70, 26, hDlg,
        (HMENU)(INT_PTR)IDCANCEL, hInst, nullptr);

    SendMessageW(GetDlgItem(hDlg, IDC_S_AUTOFIX), BM_SETCHECK,
        dlg->autoFix ? BST_CHECKED : BST_UNCHECKED, 0);

    // 设置界面字体统一为微软雅黑 12px：对话框与全部子控件
    dlg->font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            L"Microsoft YaHei");
    SendMessageW(hDlg, WM_SETFONT, (WPARAM)dlg->font, TRUE);
    EnumChildWindows(hDlg, SetChildFont, (LPARAM)dlg->font);
}

static const wchar_t* kSettingsClass = L"DragFullSettingsCls";
static bool RegisterSettingsClass() {
    static bool done = false;
    if (done) return true;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = (WNDPROC)CDragFullPlugin::SettingsDlgProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = kSettingsClass;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (RegisterClassExW(&wc))
        done = true;
    return done;
}

CDragFullPlugin CDragFullPlugin::m_instance;

CDragFullPlugin::CDragFullPlugin() = default;

CDragFullPlugin::~CDragFullPlugin() {
    SaveConfig();   // 退出时落盘“自动修复”开关
}

CDragFullPlugin& CDragFullPlugin::Instance() {
    return m_instance;
}

///////////////////////////////////////////////////////////////////////////////
// 只有一个显示项，index 必须为 0，其它一律返回 nullptr
IPluginItem* CDragFullPlugin::GetItem(int index) {
    return (index == 0) ? &m_item : nullptr;
}

void CDragFullPlugin::DataRequired() {
    ++m_cycle;

    // 轻量探测：一次 user32 调用，开销可忽略
    bool on = DragFullWindows::Read();
    DragState now = on ? DragState::On : DragState::Off;

    // 自动修复（守护）：按设定的检查间隔执行，Citrix 把设置关掉后自动纠正
    if (m_autoFix && !on) {
        ULONGLONG nowTick = GetTickCount64();
        if ((nowTick - m_lastCheckTick) >= (ULONGLONG)m_checkIntervalSec * 1000ULL) {
            m_lastCheckTick = nowTick;
            if (ShouldRetry()) {
                DragState r = DragFullWindows::Apply();
                if (r == DragState::On) {
                    now = DragState::On;
                    if (m_app)
                        m_app->ShowNotifyMessage(L"已自动恢复：拖动窗口显示内容");
                } else {
                    now = DragState::Suppressed;
                    m_lastSuppressTick = nowTick;   // 进入退避
                }
            }
        }
    }

    m_state = now;

    // Tooltip：状态变化时立即重建；否则每 5 个周期重建一次（DWM/进程扫描较重）
    if (now != m_lastTooltipState || (m_cycle % 5) == 0) {
        RebuildTooltip();
        m_lastTooltipState = now;
    }
}

const wchar_t* CDragFullPlugin::StateText() const {
    switch (m_state) {
        case DragState::On:         return L"开";
        case DragState::Suppressed: return L"被压制";
        case DragState::Off:
        case DragState::Unknown:
        default:                    return L"关";
    }
}

void CDragFullPlugin::FixNow() {
    DragState r = DragFullWindows::Apply();
    m_state = r;
    RebuildTooltip();
    m_lastTooltipState = r;

    if (m_app) {
        if (r == DragState::On)
            m_app->ShowNotifyMessage(L"已修复：拖动窗口现在显示内容");
        else if (r == DragState::Suppressed)
            m_app->ShowNotifyMessage(L"修复失败：可能被 Citrix 持续压制，请退出虚拟应用后重试");
    }
}

void CDragFullPlugin::SetAutoFix(bool v) {
    m_autoFix = v;
    SaveConfig();
}

const wchar_t* CDragFullPlugin::GetInfo(PluginInfoIndex index) {
    switch (index) {
        case TMI_NAME:        return L"拖动显示修复";
        case TMI_DESCRIPTION: return L"显示并一键修复「拖动窗口只剩虚线框」问题（Citrix 会关闭拖动时显示窗口内容）";
        case TMI_AUTHOR:      return L"Ben";
        case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
        case TMI_VERSION:     return L"V2026.10.23.1";
        case TMI_URL:         return L"";
        default:              return L"";
    }
}

const wchar_t* CDragFullPlugin::GetTooltipInfo() {
    return m_tooltip.c_str();
}

void CDragFullPlugin::OnInitialize(ITrafficMonitor* pApp) {
    m_app = pApp;
    if (pApp)
        LoadConfig(std::wstring(pApp->GetPluginConfigDir()));
    // 先探一次，避免首屏 Tooltip 为空
    m_state = DragFullWindows::Read() ? DragState::On : DragState::Off;
    RebuildTooltip();
    m_lastTooltipState = m_state;
}

int CDragFullPlugin::GetCommandCount() {
    return 2;   // 0: 立即修复  1: 自动修复（守护）
}

const wchar_t* CDragFullPlugin::GetCommandName(int command_index) {
    switch (command_index) {
        case 0: return L"立即修复";
        case 1: return L"自动修复（守护）";
        default: return nullptr;
    }
}

void CDragFullPlugin::OnPluginCommand(int command_index, void* /*hWnd*/, void* /*para*/) {
    switch (command_index) {
        case 0: FixNow(); break;
        case 1: SetAutoFix(!m_autoFix); break;
        default: break;
    }
}

int CDragFullPlugin::IsCommandChecked(int command_index) {
    if (command_index == 1)
        return m_autoFix ? 1 : 0;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
void CDragFullPlugin::LoadConfig(const std::wstring& config_dir) {
    std::wstring dir = config_dir;
    if (!dir.empty() && dir.back() != L'\\')
        dir += L'\\';
    m_config_path = dir + L"TrafficMonitorCTXDFW.ini";
    const wchar_t* ini = m_config_path.c_str();

    wchar_t buf[16] = {0};
    GetPrivateProfileStringW(L"Settings", L"AutoFix", L"0", buf, 16, ini);
    m_autoFix = (buf[0] == L'1');

    // 图标颜色：以 RRGGBB 十六进制存储，默认绿
    GetPrivateProfileStringW(L"Settings", L"IconColor", L"3CB44B", buf, 16, ini);
    m_iconColor = ParseHexColor(buf, RGB(60, 180, 75));

    // 检查间隔（秒），默认 60，限制合理范围
    int iv = GetPrivateProfileIntW(L"Settings", L"CheckInterval", 60, ini);
    if (iv < 5)     iv = 5;
    if (iv > 3600)  iv = 3600;
    m_checkIntervalSec = iv;

    // “虚”字外观：字体 / 粗体 / 字号
    wchar_t fbuf[64] = {0};
    GetPrivateProfileStringW(L"Settings", L"IconFontName", L"微软雅黑", fbuf, 64, ini);
    m_iconFontName = fbuf;
    m_iconFontBold = (GetPrivateProfileIntW(L"Settings", L"IconFontBold", 0, ini) != 0);
    int fs = GetPrivateProfileIntW(L"Settings", L"IconFontSize", 12, ini);
    if (fs < 6)  fs = 6;
    if (fs > 72) fs = 72;
    m_iconFontSize = fs;
}

void CDragFullPlugin::SaveConfig() const {
    if (m_config_path.empty())
        return;
    const wchar_t* ini = m_config_path.c_str();

    WritePrivateProfileStringW(L"Settings", L"AutoFix",
                               m_autoFix ? L"1" : L"0", ini);

    wchar_t col[8];
    swprintf_s(col, _countof(col), L"%02X%02X%02X",
               GetRValue(m_iconColor), GetGValue(m_iconColor), GetBValue(m_iconColor));
    WritePrivateProfileStringW(L"Settings", L"IconColor", col, ini);

    wchar_t iv[8];
    swprintf_s(iv, _countof(iv), L"%d", m_checkIntervalSec);
    WritePrivateProfileStringW(L"Settings", L"CheckInterval", iv, ini);

    WritePrivateProfileStringW(L"Settings", L"IconFontName", m_iconFontName.c_str(), ini);
    WritePrivateProfileStringW(L"Settings", L"IconFontBold",
                               m_iconFontBold ? L"1" : L"0", ini);
    wchar_t sz[8];
    swprintf_s(sz, _countof(sz), L"%d", m_iconFontSize);
    WritePrivateProfileStringW(L"Settings", L"IconFontSize", sz, ini);
}

void CDragFullPlugin::RebuildTooltip() {
    std::wstring s;
    s += L"拖动时显示窗口内容\n";
    s += L"运行时状态: ";
    switch (m_state) {
        case DragState::On:         s += L"开 (ON)"; break;
        case DragState::Suppressed: s += L"被压制"; break;
        case DragState::Off:        s += L"关 (OFF)"; break;
        default:                    s += L"未知"; break;
    }
    s += L"\n注册表值(HKCU): ";
    s += DragFullWindows::ReadRegistryValue() ? L"1 (开)" : L"0 (关)";
    s += L"\nDWM 桌面合成: ";
    s += DragFullWindows::IsDwmCompositionEnabled() ? L"已启用" : L"已关闭(此时等效关)";
    s += L"\nCitrix 会话: ";
    s += DragFullWindows::IsCitrixSessionRunning() ? L"运行中(可能压制)" : L"未运行";
    s += L"\n\n左键单击 = 立即修复    右键 = 菜单";
    m_tooltip = s;
}

bool CDragFullPlugin::ShouldRetry() const {
    // 被压制后退避 30 秒，避免死循环重试
    if (m_lastSuppressTick == 0)
        return true;
    return (GetTickCount64() - m_lastSuppressTick) > 30000ULL;
}

///////////////////////////////////////////////////////////////////////////////
// DLL 导出：返回全局单例（框架不负责释放）
void CDragFullPlugin::RefreshTooltip() {
    RebuildTooltip();
    m_lastTooltipState = m_state;
}

HFONT CDragFullPlugin::CreateIconFont() const {
    return CreateFontW(-m_iconFontSize, 0, 0, 0,
                       m_iconFontBold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       m_iconFontName.empty() ? L"Microsoft YaHei"
                                              : m_iconFontName.c_str());
}

/////////////////////////////////////////////////////////////////////////////
// 选项设置对话框：手动创建的模态窗口（无 .rc 资源），自带 确认/取消/应用
INT_PTR CALLBACK CDragFullPlugin::SettingsDlgProc(HWND hDlg, UINT msg,
                                                  WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id   = (int)LOWORD(wParam);
        int code = (int)HIWORD(wParam);
        DlgData* dlg = (DlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

        // “虚”字外观实时预览：字体 / 字号 / 粗体 变更即重建预览
        if (id == IDC_S_FONT || id == IDC_S_SIZE) {
            if (code == CBN_SELCHANGE)
                UpdateIconPreviewFont(hDlg, dlg);
        } else if (id == IDC_S_BOLD) {
            if (code == BN_CLICKED)
                UpdateIconPreviewFont(hDlg, dlg);
        } else if (code == BN_CLICKED) {
            if (id == IDC_S_CHOOSECOLOR) {
                CHOOSECOLORW cc{};
                static COLORREF custColors[16] = {0};
                cc.lStructSize  = sizeof(cc);
                cc.hwndOwner    = hDlg;
                cc.lpCustColors = custColors;
                cc.rgbResult    = dlg->color;
                cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    dlg->color = cc.rgbResult;
                    if (dlg->brush) DeleteObject(dlg->brush);
                    dlg->brush = CreateSolidBrush(dlg->color);
                    InvalidateRect(GetDlgItem(hDlg, IDC_S_PREVIEW), nullptr, TRUE);
                    InvalidateRect(GetDlgItem(hDlg, IDC_S_PREVIEW2), nullptr, TRUE);
                }
            } else if (id == IDC_S_APPLY) {
                ApplySettings(hDlg, dlg);     // 应用：立即生效，不关闭
            } else if (id == IDOK) {
                ApplySettings(hDlg, dlg);
                DestroyWindow(hDlg);
            } else if (id == IDCANCEL) {
                DestroyWindow(hDlg);          // 取消：丢弃，不写回
            }
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HWND hCtrl = (HWND)lParam;
        DlgData* dlg = (DlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        if (hCtrl == GetDlgItem(hDlg, IDC_S_PREVIEW)) {
            SetBkColor((HDC)wParam, dlg->color);
            return (INT_PTR)dlg->brush;
        }
        if (hCtrl == GetDlgItem(hDlg, IDC_S_PREVIEW2)) {   // “虚”字预览
            if (dlg->iconFont) SelectObject((HDC)wParam, dlg->iconFont);
            SetBkColor((HDC)wParam, RGB(255, 255, 255));
            SetTextColor((HDC)wParam, dlg->color);
            return (INT_PTR)GetStockObject(WHITE_BRUSH);
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hDlg);   // 点右上角 X 视为取消
        return 0;

    case WM_DESTROY: {
        DlgData* dlg = (DlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        if (dlg) {
            if (dlg->brush) DeleteObject(dlg->brush);
            if (dlg->font)  DeleteObject(dlg->font);
            if (dlg->iconFont) DeleteObject(dlg->iconFont);
            delete dlg;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
        }
        g_plugin.m_settingsClosed = true;
        // 唤醒 GetMessage，使 ShowOptionsDialog 的消息循环退出
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
        return 0;
    }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

ITMPlugin::OptionReturn CDragFullPlugin::ShowOptionsDialog(void* hParent) {
    if (!RegisterSettingsClass())
        return OR_OPTION_NOT_PROVIDED;

    m_settingsClosed = false;
    m_settingsResult = OR_OPTION_UNCHANGED;

    HWND hParentWnd = reinterpret_cast<HWND>(hParent);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 340, h = 252;
    int x = (sw - w) / 2;
    int y = (sh - h) / 2;

    HWND hDlg = CreateWindowExW(
        0, kSettingsClass, L"拖动显示修复 - 设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h, hParentWnd, nullptr,
        GetModuleHandle(nullptr), nullptr);
    if (!hDlg)
        return OR_OPTION_NOT_PROVIDED;

    BuildSettingsControls(hDlg);

    if (hParentWnd)
        EnableWindow(hParentWnd, FALSE);

    // 手动模态消息循环：阻塞直到对话框销毁
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (m_settingsClosed)
            break;
    }

    if (hParentWnd)
        EnableWindow(hParentWnd, TRUE);

    return m_settingsResult;
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() {
    return &CDragFullPlugin::Instance();
}
