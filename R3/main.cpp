#include "../Common/SymbolLoader.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define IOCTL_LOAD_SYMBOLS        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

static SYMBOLS_DATA g_SymbolsData = { 0 };

static void __cdecl ConsoleLog(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
}

int main(int argc, char** argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    printf("========================================\n");
    printf(" Amd-V-ReloadDbg R3 (symbol loader)\n");
    printf("========================================\n");

    if (!SymbolLoadAndResolve(&g_SymbolsData, ConsoleLog)) {
        printf("[!] symbol loading failed\n");
        return 1;
    }

    printf("\n[+] all symbols resolved.\n");

    HANDLE hDevice = CreateFileA("\\\\.\\YCData", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("[!] Open \\\\.\\YCData failed: %lu (is the driver loaded?)\n", GetLastError());
        SymbolCleanup();
        return 1;
    }

    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(hDevice, IOCTL_LOAD_SYMBOLS, &g_SymbolsData, sizeof(g_SymbolsData), NULL, 0, &bytes, NULL);
    if (!ok) {
        printf("[!] IOCTL_LOAD_SYMBOLS failed: %lu\n", GetLastError());
        CloseHandle(hDevice);
        SymbolCleanup();
        return 1;
    }
    printf("[+] IOCTL_LOAD_SYMBOLS ok (hooks installed)\n");

    CloseHandle(hDevice);
    SymbolCleanup();
    printf("done.\n");
    system("pause");
    return 0;
}
