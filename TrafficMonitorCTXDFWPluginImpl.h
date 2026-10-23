#pragma once
#include "PluginInterface.h"
#include "TrafficMonitorCTXDFW.h"
#include "TrafficMonitorCTXDFWItem.h"

#include <string>

// 插件本体（实现 ITMPlugin）。
// 用法：
//   - 在 TrafficMonitor 显示区显示「虚」字（插件项默认排在「英」等内置输入法项的右边）。
//   - 「虚」字状态着色：检测到「拖动只剩虚线框」→ 高亮(亮红)；未被处理(被压制) → 变暗(灰)；
//     正常开启 → 用户设置色（默认绿）。
//   - 双击「虚」字 → 按需修复（正常则不修复，仅提示）；显示项右键 → 宿主菜单（立即修复 / 自动修复 / 诊断详情）。
//   - 开启「自动修复」后，宿主每秒调用的 DataRequired() 会替你守护：Citrix 一关掉就秒恢复。
class CDragFullPlugin : public ITMPlugin {
public:
    CDragFullPlugin();
    virtual ~CDragFullPlugin();

    static CDragFullPlugin& Instance();

    // ---- ITMPlugin 接口（不要重写 GetAPIVersion，使用默认 7）----
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void OnInitialize(ITrafficMonitor* pApp) override;
    virtual int GetCommandCount() override;
    virtual const wchar_t* GetCommandName(int command_index) override;
    virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    virtual int IsCommandChecked(int command_index) override;

    // ---- 供显示项 / 命令调用的辅助方法 ----
    DragState GetState() const { return m_state; }
    const wchar_t* StateText() const;   // L"开" / L"关" / L"被压制"
    COLORREF StateColor() const;        // 按状态着色：高亮(亮红) / 变暗(灰) / 用户色
    void FixNow();                      // 立即执行一次修复
    void FixNowIfNeeded();              // 双击“虚”字：仅异常时修复，正常则提示无需修复
    bool IsAutoFix() const { return m_autoFix; }
    void SetAutoFix(bool v);

    // ---- 设置项访问 ----
    COLORREF GetIconColor() const { return m_iconColor; }
    void SetIconColor(COLORREF c) { m_iconColor = c; }
    int GetCheckInterval() const { return m_checkIntervalSec; }
    void SetCheckInterval(int s) { m_checkIntervalSec = s; }
    void MarkOptionChanged() { m_settingsResult = ITMPlugin::OR_OPTION_CHANGED; }

    // ---- “虚”字外观设置项 ----
    const std::wstring& GetIconFontName() const { return m_iconFontName; }
    void SetIconFontName(const std::wstring& s) { m_iconFontName = s; }
    bool GetIconFontBold() const { return m_iconFontBold; }
    void SetIconFontBold(bool b) { m_iconFontBold = b; }
    int  GetIconFontSize() const { return m_iconFontSize; }
    void SetIconFontSize(int s) { m_iconFontSize = s; }

    // 自定义显示字（默认“虚”，输入框限制为单个汉字）
    const std::wstring& GetIconChar() const { return m_iconChar; }
    void SetIconChar(const std::wstring& s) { m_iconChar = s.empty() ? L"虚" : s; }
    HFONT CreateIconFont() const;   // 按当前“虚”字设置创建字体，调用方负责 DeleteObject

    // ---- 选项设置对话框（含 确认 / 取消 / 应用）----
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

    // ---- 诊断详情 ----
    void ShowDiagDialog(void* hParent);   // 弹出诊断详情对话框

    // ---- 供设置对话框 / 应用逻辑调用 ----
    void SaveConfig() const;
    void RefreshTooltip();

private:
    void LoadConfig(const std::wstring& config_dir);
    void RebuildTooltip();              // 重建完整诊断文本（较重，低频调用）
    bool ShouldRetry() const;           // 被压制后的退避判断

    static CDragFullPlugin m_instance;
    CDragFullItem m_item;
    ITrafficMonitor* m_app = nullptr;
    DragState m_state = DragState::Unknown;
    bool m_autoFix = false;
    std::wstring m_config_path;
    std::wstring m_tooltip;
    ULONGLONG m_lastSuppressTick = 0;   // GetTickCount64()，用于压制退避
    ULONGLONG m_cycle = 0;              // DataRequired 调用计数
    DragState m_lastTooltipState = DragState::Unknown;

    // 设置项
    COLORREF m_iconColor = RGB(60, 180, 75);  // “虚”图标颜色（用户可在设置中调整）
    int m_checkIntervalSec = 60;             // 自动检查间隔（秒），默认 1 分钟
    std::wstring m_iconFontName = L"微软雅黑";  // “虚”字字体
    bool        m_iconFontBold  = false;       // “虚”字是否粗体
    int         m_iconFontSize  = 12;          // “虚”字号（px）
    std::wstring m_iconChar     = L"虚";        // 自定义显示字（默认“虚”）
    ULONGLONG m_lastCheckTick = 0;           // 上次执行检查的时刻（GetTickCount64）
    bool m_settingsClosed = false;          // 设置对话框消息循环退出标志
    OptionReturn m_settingsResult = OR_OPTION_UNCHANGED;
};

// 与官方模板一致的全局访问宏
#define g_plugin (CDragFullPlugin::Instance())
