#pragma once
#include "PluginInterface.h"

// 显示项（实现 IPluginItem）。
// 采用自绘（IsCustomDraw = true），用颜色区分开/关/被压制，并接管左键单击做“立即修复”。
class CDragFullItem : public IPluginItem {
public:
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    virtual int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;
};
