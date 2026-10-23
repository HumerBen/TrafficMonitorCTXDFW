#include "TrafficMonitorCTXDFW.h"

#include <dwmapi.h>
#include <tlhelp32.h>
#include <cwchar>
#include <string>

#pragma comment(lib, "dwmapi.lib")

// 正确的常量 —— 绝不能用 14 / 15（那是 SPI_GET/SETICONTITLELOGFONT，与拖动无关）。
// 用错后 API 会“成功返回 TRUE”却改的是字体参数，且读取时会写出 92 字节 LOGFONT，存在内存破坏风险。
static constexpr UINT SPI_GETDRAGFULLWINDOWS_V = 0x0026;  // 38
static constexpr UINT SPI_SETDRAGFULLWINDOWS_V = 0x0025;  // 37
static constexpr UINT SPIF_UPDATEANDSEND =
    SPIF_UPDATEINIFILE   // 0x0001 写入用户配置文件（注册表）
    | SPIF_SENDCHANGE;   // 0x0002 广播 WM_SETTINGCHANGE

bool DragFullWindows::Read() {
    BOOL v = FALSE;
    // GET：值通过 pvParam 返回（这里是 BOOL*）
    if (!SystemParametersInfo(SPI_GETDRAGFULLWINDOWS_V, 0, &v, 0))
        return false;
    return v != FALSE;
}

DragState DragFullWindows::Apply() {
    // 关键 1：开关值在 uiParam，pvParam 必须为 NULL
    // 关键 2：必须先 OFF 再 ON，制造真实跳变才会触发刷新
    SystemParametersInfo(SPI_SETDRAGFULLWINDOWS_V, FALSE, nullptr, SPIF_UPDATEANDSEND);
    Sleep(150);
    SystemParametersInfo(SPI_SETDRAGFULLWINDOWS_V, TRUE, nullptr, SPIF_UPDATEANDSEND);

    // 关键 3：显式广播，确保 Explorer 及各进程重新读取
    DWORD_PTR res = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        (LPARAM)L"Desktop", SMTO_ABORTIFHUNG, 5000, &res);

    // 兜底：同步注册表，供下次登录使用（Citrix 只改运行时、不刷注册表，所以写注册表只是兜底）
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0,
                      KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        const wchar_t val[] = L"1";
        RegSetValueExW(hk, L"DragFullWindows", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(val), sizeof(val));
        RegCloseKey(hk);
    }

    Sleep(300);
    if (Read())
        return DragState::On;

    // 读回仍为 0：外部（Citrix）正在持续压制
    return DragState::Suppressed;
}

bool DragFullWindows::IsDwmCompositionEnabled() {
    BOOL enabled = FALSE;
    return SUCCEEDED(DwmIsCompositionEnabled(&enabled)) && enabled;
}

bool DragFullWindows::IsCitrixSessionRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"wfica32.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"concentr.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool DragFullWindows::ReadRegistryValue() {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0,
                      KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return true;  // 键不存在时按默认 ON 处理

    wchar_t buf[16]{};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    bool on = true;
    if (RegQueryValueExW(hk, L"DragFullWindows", nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
        on = (wcscmp(buf, L"0") != 0);  // 非 "0" 即视为开
    }
    RegCloseKey(hk);
    return on;
}
