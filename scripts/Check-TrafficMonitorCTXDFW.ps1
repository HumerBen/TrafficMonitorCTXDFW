<#
.SYNOPSIS
    查看本机「拖动时显示窗口内容」的运行时状态（真正生效的那一份）。
.DESCRIPTION
    与 TrafficMonitor 插件 DragFullWindows 的只读逻辑完全对应：
      - 走 SystemParametersInfo(SPI_GETDRAGFULLWINDOWS = 0x26/38)，而非注册表。
      - 同时回显注册表 HKCU\Control Panel\Desktop\DragFullWindows 供对照。
    陷阱：绝不要用 14/15（那是图标标题字体 ICONTITLELOGFONT），
          用它读取会写出 92 字节 LOGFONT，存在内存破坏风险。
#>
$code = @'
using System;
using System.Runtime.InteropServices;
public class SPI {
    public const uint SPI_GETDRAGFULLWINDOWS = 0x0026;
    public const uint SPI_SETDRAGFULLWINDOWS = 0x0025;
    public const uint SPIF_UPDATEINIFILE = 0x1;
    public const uint SPIF_SENDCHANGE    = 0x2;
    [DllImport("user32.dll")] public static extern bool Get(uint uiAction, uint uiParam, ref bool pvParam, uint fWinIni);
    [DllImport("user32.dll")] public static extern bool Set(uint uiAction, uint uiParam, IntPtr pvParam, uint fWinIni);
}
'@
Add-Type -TypeDefinition $code -ErrorAction Stop

$on = $false
[SPI]::Get([SPI]::SPI_GETDRAGFULLWINDOWS, 0, [ref]$on, 0) | Out-Null

$reg = (Get-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "DragFullWindows" -ErrorAction SilentlyContinue).DragFullWindows

Write-Host ("运行时状态 : {0} ({1})" -f $(if ($on) { "开 ON" } else { "关 OFF" }),
                                      $(if ($on) { "拖动显示内容" } else { "拖动只剩虚线框" }))
Write-Host ("注册表值   : {0}" -f $(if ($null -eq $reg) { "(不存在，默认开)" } else { $reg }))

if (-not $on) {
    Write-Warning "当前拖动窗口将只剩虚线框。可用 Apply-DragFullWindows.ps1 立即修复，或用插件左键单击。"
}
