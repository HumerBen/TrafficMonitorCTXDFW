# DragFullWindows —— TrafficMonitor 插件

显示本机是否处于「拖动窗口只剩虚线框、不显示内容」的状态（Citrix 无缝虚拟应用会悄悄关闭它），
并在**左键单击 / 右键菜单**时一键修复，无需注销或重启。

## 原理（一句话）

Windows 的「拖动时显示窗口内容」有两份状态：注册表存的是**下次登录**用的，运行时缓存才是**当下生效**的。
Citrix 只改了后者且不刷新前者，所以改注册表无效。正确做法是用
`SystemParametersInfo(SPI_SETDRAGFULLWINDOWS=37)` **先 OFF 再 ON**（开关值放 `uiParam`，`pvParam` 传 `NULL`）
并带 `SPIF_UPDATEINIFILE | SPIF_SENDCHANGE` 广播，即时生效。完整原理（双层状态模型、常量陷阱、toggle 必要性等）见你提供的《DragFullWindows 状态显示与修复》长文。

## 功能

- **主显示**：自绘「虚」字图标，颜色可在设置中自定义（默认绿色）。
- **左键单击** → 立即执行一次修复（状态变化见 Tooltip）。
- **右键菜单**（宿主提供）→ `立即修复` / `自动修复（守护）`。
- **选项设置对话框**：可配置「图标颜色」「检查间隔（秒）」「自动修复（守护）」，提供 **确认 / 取消 / 应用** 三个按钮；点「应用」立即生效且**不关闭**对话框。
- **自动修复（守护）**：开启后，插件按「检查间隔」周期性（默认每 60 秒）调用 `DataRequired()`，发现被关闭即自动恢复，无感知。
- **Tooltip**：悬停显示运行时状态、注册表值、DWM 桌面合成、Citrix 是否在跑，方便诊断。
- 配置持久化到 `插件配置目录\DragFullWindows.ini`（`AutoFix` / `IconColor` / `CheckInterval`）。

## 构建

 prerequisities：Visual Studio 2019/2022，「使用 C++ 的桌面开发」工作负载（含 Windows SDK）。
**不需要 MFC**。

1. 用 Visual Studio 打开 `TrafficMonitorCTXDFWPlugin.vcxproj`。
2. 选择配置：`Release` + `x64`（TrafficMonitor 当前为 64 位；若你用的是 32 位旧版，则选 `Win32`）。
3. 生成（Build）。输出：`TrafficMonitorCTXDFWPlugin\bin\x64\Release\TrafficMonitorCTXDFW.dll`。

> 纯 Win32 实现（无 MFC、无资源脚本），只依赖 `user32`（默认链接）、`dwmapi` 与 `comdlg32`（`ChooseColor` 取色对话框需要）。

## 部署

1. 把 `TrafficMonitorCTXDFW.dll` 复制到 TrafficMonitor 主程序所在目录的 `plugins` 子目录
   （即与 `TrafficMonitor.exe` 同级下的 `plugins\`；没有就新建）。
2. 重启 TrafficMonitor。
3. 任务栏右键 → **其他功能** → **插件管理**，勾选「拖动显示修复」并确认启用。

## 使用

- 任务栏显示「虚」字图标，颜色即你在设置中指定的颜色。
- **左键单击「虚」图标**立即触发一次修复（修复失败可看 Tooltip 诊断）。
- 想周期性自动守护：右键菜单 → **自动修复（守护）** 打勾；之后插件按「检查间隔」（默认 60 秒）自动恢复。
- 右键菜单 → **设置**（或插件管理中的选项入口）打开设置对话框，调整图标颜色、检查间隔、自动修复开关，点「应用」即时生效。

## 目录结构

```
TrafficMonitorCTXDFWPlugin/
├── PluginInterface.h      # 官方插件接口（权威，勿改）
├── TrafficMonitorCTXDFW.h/.cpp # 与框架解耦的核心引擎（只读 / 修复 / 诊断）
├── TrafficMonitorCTXDFWPluginImpl.h/.cpp  # ITMPlugin 实现（显示项管理 / 命令 / 配置 / 守护）
├── TrafficMonitorCTXDFWItem.h/.cpp    # IPluginItem 实现（自绘 + 左键修复）
└── TrafficMonitorCTXDFWPlugin.vcxproj

scripts/
├── Check-TrafficMonitorCTXDFW.ps1  # 查看运行时状态（与插件只读逻辑一致，用于对照验收）
└── Apply-TrafficMonitorCTXDFW.ps1  # 手动修复（与插件 FixNow 一致）
```

## 验收要点（对照你给的清单）

- [ ] 代码中**没有**出现常量 `14` / `15`。
- [ ] `SPI_SETDRAGFULLWINDOWS` 调用中 `pvParam` 为 `nullptr`。
- [ ] 走的是 OFF→ON 两步 toggle，而非单次设 ON。
- [ ] `fWinIni` 带 `SPIF_UPDATEINIFILE | SPIF_SENDCHANGE`。
- [ ] 用 `SystemParametersInfo(38, ...)` 读取的值，与 `scripts/Check-TrafficMonitorCTXDFW.ps1` 结果一致。
- [ ] 修复后仍读回 0 时，显示「被压制」而非无限重试（已做 30 秒退避）。

## 已知限制

- 若 Citrix 会话持续压制（修复后读回仍是 0），任何手段都无效，需退出虚拟应用；插件会显示「被压制」并退避重试。
- DWM 桌面合成被关（`VisualFXSetting=2` 调整为最佳性能）时，`DragFullWindows=1` 也形同虚设，属系统层限制。
