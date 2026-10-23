<#
.SYNOPSIS
    一键修复「拖动时显示窗口内容」被关闭的问题（对应插件的 FixNow）。
.DESCRIPTION
    关键三步（与插件 C++ 实现一致）：
      1) 先 SPI_SET(37, OFF, NULL) 再 SPI_SET(37, ON, NULL)
         —— 制造真实的 0->1 跳变，系统才会触发刷新；只设一次 ON 会被判“无变化”而跳过。
      2) 显式广播 WM_SETTINGCHANGE(0x1A) 到 HWND_BROADCAST，确保各进程重读。
      3) 写注册表 HKCU\...\Desktop\DragFullWindows="1" 作为下次登录兜底。
    无需注销 / 重启 / 管理员权限。
    若修复后仍读回 0，说明被 Citrix (wfica32.exe / concentr.exe) 持续压制。
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
public class Win {
    public const int  HWND_BROADCAST    = 0xFFFF;
    public const uint WM_SETTINGCHANGE  = 0x001A;
    public const uint SMTO_ABORTIFHUNG  = 0x2;
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, IntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
}
'@
Add-Type -TypeDefinition $code -ErrorAction Stop

$flags = [SPI]::SPIF_UPDATEINIFILE -bor [SPI]::SPIF_SENDCHANGE

# 关键：先 OFF 再 ON，制造真实跳变
[SPI]::Set([SPI]::SPI_SETDRAGFULLWINDOWS, 0, [IntPtr]::Zero, $flags) | Out-Null
Start-Sleep -Milliseconds 150
[SPI]::Set([SPI]::SPI_SETDRAGFULLWINDOWS, 1, [IntPtr]::Zero, $flags) | Out-Null

# 显式广播
$r = [UIntPtr]::Zero
[Win]::SendMessageTimeout([IntPtr][Win]::HWND_BROADCAST, [Win]::WM_SETTINGCHANGE,
                          [IntPtr]::Zero, "Desktop", [Win]::SMTO_ABORTIFHUNG, 5000, [ref]$r) | Out-Null

# 注册表兜底
Set-ItemProperty -Path "HKCU:\Control Panel\Desktop" -Name "DragFullWindows" -Value "1" -Type String -Force

# 校验
Start-Sleep -Milliseconds 300
$on = $false
[SPI]::Get([SPI]::SPI_GETDRAGFULLWINDOWS, 0, [ref]$on, 0) | Out-Null

if ($on) {
    Write-Host "修复成功：运行时状态已为 开(ON)，拖动窗口现在显示内容。" -ForegroundColor Green
} else {
    Write-Warning "修复后仍读回 0 —— 可能被 Citrix (wfica32/concentr) 持续压制。请退出虚拟应用后重试。"
}
