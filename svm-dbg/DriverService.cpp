#include "DriverService.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>

#pragma comment(lib, "advapi32.lib")

static SVC_LOG_CB g_log = NULL;

static void Log(const wchar_t* fmt, ...)
{
    if (!g_log)
        return;
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(buf);
}

static void LogError(const wchar_t* what, DWORD err)
{
    Log(L"[!] %s failed: %lu", what, err);
}

static BOOL GetExeDir(wchar_t* out, size_t len)
{
    DWORD n = GetModuleFileNameW(NULL, out, (DWORD)len);
    if (n == 0)
        return FALSE;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash)
        *(slash + 1) = 0;
    return TRUE;
}

BOOL DriverLoad(SVC_LOG_CB log)
{
    g_log = log;

    wchar_t exeDir[MAX_PATH];
    wchar_t sysPath[MAX_PATH];
    if (!GetExeDir(exeDir, MAX_PATH))
        return FALSE;
    swprintf_s(sysPath, L"%s%s", exeDir, DRV_SYS_FILENAME);

    if (GetFileAttributesW(sysPath) == INVALID_FILE_ATTRIBUTES) {
        Log(L"[!] Driver file not found: %s", sysPath);
        return FALSE;
    }

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        LogError(L"OpenSCManager", GetLastError());
        return FALSE;
    }

    SC_HANDLE svc = OpenServiceW(scm, DRV_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!svc) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_DOES_NOT_EXIST) {
            LogError(L"OpenService", err);
            CloseServiceHandle(scm);
            return FALSE;
        }
        svc = CreateServiceW(
            scm,
            DRV_SERVICE_NAME,
            DRV_SERVICE_NAME,
            SERVICE_ALL_ACCESS,
            SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL,
            sysPath,
            NULL, NULL, NULL, NULL, NULL);
        if (!svc) {
            LogError(L"CreateService", GetLastError());
            CloseServiceHandle(scm);
            return FALSE;
        }
        Log(L"[+] Driver service registered");
    }

    // The service may have been created by an older build with a stale or
    // quoted ImagePath. Keep the existing service, but always synchronize its
    // binary path with the driver next to this executable.
    if (!ChangeServiceConfigW(
        svc,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        sysPath,
        NULL, NULL, NULL, NULL, NULL, DRV_SERVICE_NAME))
    {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_MARKED_FOR_DELETE) {
            LogError(L"ChangeServiceConfig", err);
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return FALSE;
        }
    }

    Log(L"[*] Driver path: %s", sysPath);

    BOOL ok = StartServiceW(svc, 0, NULL);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            ok = TRUE;
            Log(L"[+] Driver already running");
        } else {
            LogError(L"StartService", err);
        }
    } else {
        Log(L"[+] Driver loaded");
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

BOOL DriverUnload(SVC_LOG_CB log)
{
    g_log = log;

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        LogError(L"OpenSCManager", GetLastError());
        return FALSE;
    }

    SC_HANDLE svc = OpenServiceW(scm, DRV_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!svc) {
        LogError(L"OpenService", GetLastError());
        CloseServiceHandle(scm);
        return FALSE;
    }

    SERVICE_STATUS ss;
    BOOL ok = ControlService(svc, SERVICE_CONTROL_STOP, &ss);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            ok = TRUE;
            Log(L"[+] Driver not running");
        } else {
            LogError(L"ControlService(STOP)", err);
        }
    } else {
        Log(L"[+] Driver unloaded");
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}
