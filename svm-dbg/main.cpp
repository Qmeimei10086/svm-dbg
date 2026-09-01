#include "../Common/SymbolLoader.h"
#include "DriverService.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>

#pragma comment(lib, "advapi32.lib")

#define IOCTL_LOAD_SYMBOLS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IDC_LOG            1001
#define IDC_BTN_ENTER_VM   1002
#define IDC_BTN_EXIT_VM    1003
#define IDC_BTN_REBUILD    1004
#define IDC_CHK_CREATE     1005

#define WM_APP_SYMBOLS_DONE (WM_APP + 1)

static SYMBOLS_DATA g_SymbolsData = { 0 };
static BOOL g_symbolsOk = FALSE;
static BOOL g_inVm = FALSE;
static BOOL g_debugBuilt = FALSE;
static volatile LONG g_closing = 0;
static HFONT g_font = NULL;

static HWND g_hMainWnd = NULL;
static HWND g_hLog = NULL;
static HWND g_hBtnEnter = NULL;
static HWND g_hBtnExit = NULL;
static HWND g_hBtnRebuild = NULL;
static HWND g_hChkCreate = NULL;
static HANDLE g_hSymbolThread = NULL;

#define LOG_BUF_SIZE 65536
static wchar_t g_logBuffer[LOG_BUF_SIZE] = { 0 };
static size_t g_logLen = 0;

static void LogLineW(const wchar_t* line)
{
    if (InterlockedCompareExchange(&g_closing, 0, 0) != 0)
        return;

    size_t n = wcslen(line);
    if (g_logLen + n + 3 < LOG_BUF_SIZE) {
        wcscpy_s(g_logBuffer + g_logLen, LOG_BUF_SIZE - g_logLen, line);
        g_logLen += n;
        g_logBuffer[g_logLen++] = L'\r';
        g_logBuffer[g_logLen++] = L'\n';
        g_logBuffer[g_logLen] = 0;
    }

    if (g_hLog && IsWindow(g_hLog)) {
        wchar_t tmp[512];
        swprintf_s(tmp, _countof(tmp), L"%s\r\n", line);
        int len = GetWindowTextLengthW(g_hLog);
        SendMessageW(g_hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)tmp);
        UpdateWindow(g_hLog);
    }
}

static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    LogLineW(buf);
}

static void __cdecl LogLineCB(const wchar_t* line)
{
    LogLineW(line);
}

static void __cdecl SymbolLog(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    wchar_t wbuf[512];
    MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, _countof(wbuf));
    LogLineW(wbuf);
}

static void UpdateControls()
{
    EnableWindow(g_hBtnEnter, g_symbolsOk && !g_inVm);
    EnableWindow(g_hBtnExit, g_inVm);
    EnableWindow(g_hBtnRebuild, g_inVm && !g_debugBuilt);
    EnableWindow(g_hChkCreate, g_inVm && !g_debugBuilt);
}

static BOOL SendSymbols()
{
    HANDLE h = CreateFileW(L"\\\\.\\YCData", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        LogW(L"[!] Open \\\\.\\YCData failed: %lu", GetLastError());
        return FALSE;
    }

    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_LOAD_SYMBOLS, &g_SymbolsData, sizeof(g_SymbolsData), NULL, 0, &bytes, NULL);
    if (!ok) {
        LogW(L"[!] IOCTL_LOAD_SYMBOLS failed: %lu", GetLastError());
        CloseHandle(h);
        return FALSE;
    }
    LogW(L"[+] Rebuild debug system OK");
    CloseHandle(h);
    return TRUE;
}

static DWORD WINAPI SymbolThread(LPVOID param)
{
    UNREFERENCED_PARAMETER(param);
    BOOL ok = SymbolLoadAndResolve(&g_SymbolsData, SymbolLog);
    // SymInitialize/SymCleanup belong to the same worker thread.
    SymbolCleanup();
    if (InterlockedCompareExchange(&g_closing, 0, 0) == 0)
        PostMessageW(g_hMainWnd, WM_APP_SYMBOLS_DONE, (WPARAM)ok, 0);
    return 0;
}

static void OnEnterVm()
{
    LogW(L"[*] Entering VM ...");
    if (DriverLoad(LogLineCB)) {
        g_inVm = TRUE;
        LogW(L"[+] VM entered, driver running");
    } else {
        LogW(L"[!] Enter VM failed");
    }
    UpdateControls();
}

static void OnExitVm()
{
    LogW(L"[*] Exiting VM ...");
    if (DriverUnload(LogLineCB)) {
        g_inVm = FALSE;
        g_debugBuilt = FALSE;
    }
    UpdateControls();
}

static void OnRebuild()
{
    if (!g_symbolsOk) {
        LogW(L"[!] Symbols not ready, cannot rebuild debug system");
        return;
    }

    BOOL createDebug = (SendMessageW(g_hChkCreate, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_SymbolsData.Flags = createDebug ? DBG_FLAG_REBUILD_CREATE_DEBUG : 0;

    LogW(createDebug
        ? L"[*] Rebuild debug system (with create-debug; may cause lag/unstable)..."
        : L"[*] Rebuild debug system ...");

    if (SendSymbols()) {
        g_debugBuilt = TRUE;
    }
    UpdateControls();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;
        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            12, 12, 700, 200, hwnd, (HMENU)IDC_LOG, GetModuleHandleW(NULL), NULL);

        g_hBtnEnter = CreateWindowExW(0, L"BUTTON", L"Enter VM",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 230, 120, 34, hwnd, (HMENU)IDC_BTN_ENTER_VM, GetModuleHandleW(NULL), NULL);

        g_hBtnExit = CreateWindowExW(0, L"BUTTON", L"Exit VM",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            150, 230, 120, 34, hwnd, (HMENU)IDC_BTN_EXIT_VM, GetModuleHandleW(NULL), NULL);

        g_hBtnRebuild = CreateWindowExW(0, L"BUTTON", L"Rebuild Debug",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            12, 280, 150, 34, hwnd, (HMENU)IDC_BTN_REBUILD, GetModuleHandleW(NULL), NULL);

        g_hChkCreate = CreateWindowExW(0, L"BUTTON", L"Rebuild Create-Debug",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            190, 286, 160, 24, hwnd, (HMENU)IDC_CHK_CREATE, GetModuleHandleW(NULL), NULL);

        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        HWND controls[] = { g_hLog, g_hBtnEnter, g_hBtnExit, g_hBtnRebuild, g_hChkCreate };
        for (int i = 0; i < _countof(controls); i++)
            SendMessageW(controls[i], WM_SETFONT, (WPARAM)g_font, TRUE);

        UpdateControls();

        LogW(L"[*] Loading kernel symbols, please wait ...");
        LogW(L"[!] Rebuild Create-Debug may increase system lag and instability.");
        g_hSymbolThread = CreateThread(NULL, 0, SymbolThread, NULL, 0, NULL);
        if (!g_hSymbolThread) {
            LogW(L"[!] Failed to create symbol loading thread: %lu", GetLastError());
            MessageBoxW(hwnd, L"Failed to create the symbol loading thread.",
                L"Startup failed", MB_ICONERROR);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    case WM_APP_SYMBOLS_DONE: {
        BOOL ok = (BOOL)wParam;
        if (ok) {
            g_symbolsOk = TRUE;
            LogW(L"[+] Kernel symbols and offsets loaded");
            UpdateControls();
        } else {
            LogW(L"[!] Kernel symbol/offset resolution failed");
            MessageBoxW(hwnd, g_logBuffer, L"Symbol load failed - details", MB_ICONERROR);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case IDC_BTN_ENTER_VM: OnEnterVm(); return 0;
        case IDC_BTN_EXIT_VM: OnExitVm(); return 0;
        case IDC_BTN_REBUILD: OnRebuild(); return 0;
        }
        break;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        if (width < 100 || height < 180)
            return 0;
        if (g_hLog)
            MoveWindow(g_hLog, 12, 12, width - 24, height - 145, TRUE);
        if (g_hBtnEnter)
            MoveWindow(g_hBtnEnter, 12, height - 115, 120, 34, TRUE);
        if (g_hBtnExit)
            MoveWindow(g_hBtnExit, 150, height - 115, 120, 34, TRUE);
        if (g_hBtnRebuild)
            MoveWindow(g_hBtnRebuild, 12, height - 70, 150, 34, TRUE);
        if (g_hChkCreate)
            MoveWindow(g_hChkCreate, 190, height - 64, 160, 24, TRUE);
        return 0;
    }
    case WM_DESTROY:
        if (g_hSymbolThread) {
            InterlockedExchange(&g_closing, 1);
            CloseHandle(g_hSymbolThread);
            g_hSymbolThread = NULL;
        }
        if (g_inVm) {
            DriverUnload(LogLineCB);
            g_inVm = FALSE;
        }
        if (g_font) DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmdline, int show)
{
    UNREFERENCED_PARAMETER(hPrev);
    UNREFERENCED_PARAMETER(cmdline);
    UNREFERENCED_PARAMETER(show);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SvmDbgMain";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"SvmDbgMain", L"AMD-V ReloadDbg",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 750, 380,
        NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
