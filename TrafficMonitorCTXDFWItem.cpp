#include "TrafficMonitorCTXDFWItem.h"
#include "TrafficMonitorCTXDFWPluginImpl.h"   // 引入 g_plugin 与 DragState

///////////////////////////////////////////////////////////////////////////////
const wchar_t* CDragFullItem::GetItemName() const {
    return L"拖动显示修复";          // 插件管理列表中的名称
}

const wchar_t* CDragFullItem::GetItemId() const {
    return L"dragfullwindows";       // 唯一 ID（仅含字母数字）
}

const wchar_t* CDragFullItem::GetItemLableText() const {
    return g_plugin.GetIconChar().c_str();   // 自定义显示字（自绘模式下被忽略，仍提供）
}

const wchar_t* CDragFullItem::GetItemValueText() const {
    return g_plugin.StateText();     // 自绘模式下被忽略，仍提供
}

const wchar_t* CDragFullItem::GetItemValueSampleText() const {
    return g_plugin.GetIconChar().c_str();   // 用于宽度估算
}

bool CDragFullItem::IsCustomDraw() const {
    return true;                     // 自绘：用颜色区分开/关/被压制
}

int CDragFullItem::GetItemWidthEx(void* hDC) const {
    // GetItemWidthEx 返回“实际像素宽度”，宿主不会按 DPI 再放大。
    // 用绘制用的 hDC 量出「虚」字的宽度再加左右内边距（随字体/字号变化）。
    HDC dc = reinterpret_cast<HDC>(hDC);
    if (!dc)
        return 40;   // 兜底宽度
    HFONT hFont = g_plugin.CreateIconFont();
    HFONT hOld  = (HFONT)SelectObject(dc, hFont);
    const wchar_t* sample = g_plugin.GetIconChar().c_str();
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, sample, static_cast<int>(wcslen(sample)), &sz);
    SelectObject(dc, hOld);
    DeleteObject(hFont);
    return sz.cx + 10;   // 左右内边距
}

void CDragFullItem::DrawItem(void* hDC, int x, int y, int w, int h, bool /*dark_mode*/) {
    HDC dc = reinterpret_cast<HDC>(hDC);
    if (!dc)
        return;

    int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);   // 透明背景，沿用宿主底色

    // “虚”字颜色按状态着色：检测到只剩虚线框→高亮红；未处理(被压制)→变暗灰；正常→用户设置色
    COLORREF iconColor = g_plugin.StateColor();

    // 按用户设置的字体 / 粗体 / 字号绘制“虚”字
    HFONT hFont = g_plugin.CreateIconFont();
    HFONT hOld  = (HFONT)SelectObject(dc, hFont);

    const wchar_t* icon = g_plugin.GetIconChar().c_str();

    // 垂直居中
    TEXTMETRIC tm = {0};
    GetTextMetrics(dc, &tm);
    int ty = y + (h - tm.tmHeight) / 2;

    // 画“虚”字
    SetTextColor(dc, iconColor);
    TextOutW(dc, x + 5, ty, icon, static_cast<int>(wcslen(icon)));

    SelectObject(dc, hOld);
    DeleteObject(hFont);
    RestoreDC(dc, saved);
}

int CDragFullItem::OnMouseEvent(MouseEventType type, int /*x*/, int /*y*/,
                                void* /*hWnd*/, int /*flag*/) {
    if (type == MT_DBCLICKED) {
        g_plugin.FixNow();   // 双击「虚」字 → 立即修复
        return 1;            // 已完整处理，宿主不再做额外响应
    }
    // 其余（单击/右键等）交给宿主默认处理（右键菜单仍由宿主提供插件命令）
    return 0;
}
