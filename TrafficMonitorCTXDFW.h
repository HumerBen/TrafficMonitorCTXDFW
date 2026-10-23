#pragma once
#include <windows.h>

// 当前“拖动时显示窗口内容”的状态。插件维护四态而非简单的开/关。
enum class DragState {
    On,         // 运行时为开（拖动显示内容）—— 绿色
    Off,        // 运行时为关（只剩虚线框）—— 红色
    Suppressed, // 修复后仍读回 0，被外部（Citrix）持续压制 —— 黄/红
    Unknown     // 尚未探测
};

// 与 TrafficMonitor 框架解耦的核心引擎，可单独单元测试。
// 关键点：
//   - 绝不读写 14 / 15（那是 ICONTITLELOGFONT），只使用 37 / 38。
//   - 修复必须先 OFF 再 ON（制造真实跳变）才会触发刷新。
//   - 开关值放在 uiParam，pvParam 必须传 NULL。
//   - fWinIni 必须带 SPIF_SENDCHANGE（广播 WM_SETTINGCHANGE）。
class DragFullWindows {
public:
    // 只读：当前真正生效的运行时状态（true = 开）
    static bool Read();

    // 执行修复：OFF->ON toggle + 广播 + 注册表兜底，返回修复后的状态
    static DragState Apply();

    // 附加诊断（用于 Tooltip）
    static bool IsDwmCompositionEnabled();   // DWM 桌面合成是否开启
    static bool IsCitrixSessionRunning();    // wfica32.exe / concentr.exe 是否在跑
    static bool ReadRegistryValue();         // 注册表 HKCU\...\Desktop\DragFullWindows
};
