#include "SymbolLoader.h"

#include <windows.h>
#include <winternl.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

#pragma comment(lib, "dbghelp.lib")

typedef LONG(NTAPI* pNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

typedef struct _SYSTEM_MODULE_INFORMATION_ENTRY {
    ULONG  Unknow1;
    ULONG  Unknow2;
    ULONG  Unknow3;
    ULONG  Unknow4;
    PVOID  Base;
    ULONG  Size;
    ULONG  Flags;
    USHORT Index;
    USHORT NameLength;
    USHORT LoadCount;
    USHORT ModuleNameOffset;
    char   ImageName[256];
} SYSTEM_MODULE_INFORMATION_ENTRY, *PSYSTEM_MODULE_INFORMATION_ENTRY;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG Count;
    SYSTEM_MODULE_INFORMATION_ENTRY Module[1];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

static pNtQuerySystemInformation g_ZwQuerySystemInformation = NULL;
static DWORD64 g_KernelBase = 0;
static char g_symbolCacheDir[MAX_PATH] = { 0 };
static PDB_LOG_CB g_log = NULL;
static SYMBOLS_DATA g_SymbolsData = { 0 };

static void Log(const char* fmt, ...)
{
    if (!g_log)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log("%s", buf);
}

// Same as the original r3.exe: table entries hold the real address of each
// field inside g_SymbolsData, and ResolveAddress writes back directly.
static struct { PVOID* pField; const char* name; } g_addrTable[] = {
    { &g_SymbolsData.NtCreateDebugObject,              "NtCreateDebugObject" },
    { &g_SymbolsData.PsGetNextProcessThread,           "PsGetNextProcessThread" },
    { &g_SymbolsData.DbgkpPostFakeThreadMessages,      "DbgkpPostFakeThreadMessages" },
    { &g_SymbolsData.DbgkpWakeTarget,                  "DbgkpWakeTarget" },
    { &g_SymbolsData.DbgkpSetProcessDebugObject,       "DbgkpSetProcessDebugObject" },
    { &g_SymbolsData.DbgkCreateThread,                 "DbgkCreateThread" },
    { &g_SymbolsData.DbgkpQueueMessage,                "DbgkpQueueMessage" },
    { &g_SymbolsData.PsCaptureExceptionPort,           "PsCaptureExceptionPort" },
    { &g_SymbolsData.DbgkpSendApiMessage,              "DbgkpSendApiMessage" },
    { &g_SymbolsData.DbgkpSendApiMessageLpc,           "DbgkpSendApiMessageLpc" },
    { &g_SymbolsData.DbgkpSendErrorMessage,            "DbgkpSendErrorMessage" },
    { &g_SymbolsData.DbgkForwardException,             "DbgkForwardException" },
    { &g_SymbolsData.DbgkpSuppressDbgMsg,              "DbgkpSuppressDbgMsg" },
    { &g_SymbolsData.DbgkpSectionToFileHandle,         "DbgkpSectionToFileHandle" },
    { &g_SymbolsData.DbgkUnMapViewOfSection,           "DbgkUnMapViewOfSection" },
    { &g_SymbolsData.DbgkpPostFakeProcessCreateMessages, "DbgkpPostFakeProcessCreateMessages" },
    { &g_SymbolsData.NtDebugActiveProcess,             "NtDebugActiveProcess" },
    { &g_SymbolsData.DbgkpMarkProcessPeb,              "DbgkpMarkProcessPeb" },
    { &g_SymbolsData.KiDispatchException,              "KiDispatchException" },
    { &g_SymbolsData.NtCreateUserProcess,              "NtCreateUserProcess" },
    { &g_SymbolsData.DbgkDebugObjectType,              "DbgkDebugObjectType" },
    { &g_SymbolsData.ObTypeIndexTable,                 "ObTypeIndexTable" },
    { &g_SymbolsData.NtTerminateProcess,               "NtTerminateProcess" },
    { &g_SymbolsData.DbgkMapViewOfSection,             "DbgkMapViewOfSection" },
    { &g_SymbolsData.DbgkSendSystemDllMessages,        "DbgkSendSystemDllMessages" },
    { &g_SymbolsData.DbgkpProcessDebugPortMutex,       "DbgkpProcessDebugPortMutex" },
};

static struct { ULONG64* pField; const char* type; const wchar_t* member; } g_offTable[] = {
    { &g_SymbolsData.Process_DebugPort,              "_EPROCESS",                L"DebugPort" },
    { &g_SymbolsData.Process_RundownProtect,         "_EPROCESS",                L"RundownProtect" },
    { &g_SymbolsData.Process_Flags,                  "_EPROCESS",                L"Flags" },
    { &g_SymbolsData.Process_SectionObject,          "_EPROCESS",                L"SectionObject" },
    { &g_SymbolsData.Process_SectionBaseAddress,     "_EPROCESS",                L"SectionBaseAddress" },
    { &g_SymbolsData.Thread_CrossThreadFlags,        "_ETHREAD",                 L"CrossThreadFlags" },
    { &g_SymbolsData.Thread_RundownProtect,          "_ETHREAD",                 L"RundownProtect" },
    { &g_SymbolsData.Thread_Win32StartAddress,       "_ETHREAD",                 L"Win32StartAddress" },
};

static BOOLEAN GetKernelModuleInfo(PVOID* pBase, char* pPath, ULONG pathLen)
{
    NTSTATUS status;
    ULONG len = 0;
    PSYSTEM_MODULE_INFORMATION buffer = NULL;

    do {
        if (buffer) free(buffer);
        buffer = (PSYSTEM_MODULE_INFORMATION)malloc(len);
        if (!buffer) return FALSE;

        status = g_ZwQuerySystemInformation(11, buffer, len, &len);
        if (status == 0xC0000004L) continue;
        if (status < 0) { free(buffer); return FALSE; }
        break;
    } while (TRUE);

    *pBase = buffer->Module[0].Base;
    if (pPath) {
        strncpy_s(pPath, pathLen, buffer->Module[0].ImageName, _TRUNCATE);
    }
    free(buffer);
    return TRUE;
}

static BOOLEAN FindCachedPdb(char* outPath, ULONG outPathLen)
{
    char pattern[MAX_PATH];
    sprintf_s(pattern, "%s\\ntkrnlmp.pdb\\*\\ntkrnlmp.pdb", g_symbolCacheDir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;

    sprintf_s(outPath, outPathLen, "%s\\ntkrnlmp.pdb\\%s\\ntkrnlmp.pdb", g_symbolCacheDir, fd.cFileName);
    FindClose(hFind);
    return TRUE;
}

static BOOLEAN InitSymHandler()
{
    char symPath[MAX_PATH * 2] = { 0 };
    sprintf_s(symPath, "SRV*%s*https://msdl.microsoft.com/download/symbols", g_symbolCacheDir);

    SymSetOptions(SYMOPT_CASE_INSENSITIVE | SYMOPT_UNDNAME);

    // fInvadeProcess = FALSE: do NOT enumerate every loaded DLL of the current
    // process. A GUI app loads many DLLs (user32/gdi32/comctl32/...), and with
    // TRUE dbghelp would try to download symbols for all of them, making it slow
    // and interfering with the kernel symbol load. We only need the kernel
    // module, which we load explicitly via SymLoadModule64.
    if (!SymInitialize((HANDLE)-1, symPath, FALSE))
        return FALSE;

    SymSetSearchPath((HANDLE)-1, symPath);
    return TRUE;
}

static BOOLEAN LoadKernelSymbols()
{
    char kernelPath[MAX_PATH] = { 0 };

    if (!GetKernelModuleInfo((PVOID*)&g_KernelBase, kernelPath, sizeof(kernelPath))) {
        Log("[!] GetKernelModuleInfo failed");
        return FALSE;
    }
    Log("[+] KernelBase = 0x%llX", g_KernelBase);

    GetModuleFileNameA(NULL, g_symbolCacheDir, MAX_PATH);
    char* pSlash = strrchr(g_symbolCacheDir, '\\');
    if (pSlash) *(pSlash + 1) = 0;
    strcat_s(g_symbolCacheDir, "symbols");
    CreateDirectoryA(g_symbolCacheDir, NULL);

    if (!InitSymHandler()) {
        Log("[!] SymInitialize failed: %lu", GetLastError());
        return FALSE;
    }
    Log("[+] SymInitialize ok");

    char systemKernelPath[MAX_PATH] = { 0 };
    GetSystemDirectoryA(systemKernelPath, sizeof(systemKernelPath));
    strcat_s(systemKernelPath, "\\ntoskrnl.exe");

    char cachedPdb[MAX_PATH] = { 0 };
    BOOLEAN hasCache = FindCachedPdb(cachedPdb, sizeof(cachedPdb));
    Log(hasCache ? "[+] cached PDB found" : "[*] no cache, downloading PDB...");

    HANDLE hFile = CreateFileA(systemKernelPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log("[!] Open %s failed: %lu", systemKernelPath, GetLastError());
        return FALSE;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (!hasCache)
        Log("[*] SymLoadModule64 downloading ntkrnlmp.pdb, may take a few minutes...");
    DWORD64 base = SymLoadModule64((HANDLE)-1, hFile, systemKernelPath, NULL, g_KernelBase, fileSize);
    CloseHandle(hFile);

    if (!base) {
        Log("[!] SymLoadModule64 failed: %lu", GetLastError());
        return FALSE;
    }

    Log("[+] %s (base=0x%llX, kernelBase=0x%llX)", hasCache ? "reused cached PDB" : "downloaded PDB", base, g_KernelBase);
    return TRUE;
}

typedef struct {
    const char* name;
    PVOID address;
} SYM_RESOLVE_CTX;

static BOOL CALLBACK EnumSymbolCallback(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext)
{
    UNREFERENCED_PARAMETER(SymbolSize);
    SYM_RESOLVE_CTX* ctx = (SYM_RESOLVE_CTX*)UserContext;
    if (strcmp(pSymInfo->Name, ctx->name) == 0) {
        ctx->address = (PVOID)pSymInfo->Address;
        return FALSE;
    }
    return TRUE;
}

static BOOLEAN ResolveAddress(PVOID* pField, const char* name)
{
    SYM_RESOLVE_CTX ctx = { name, NULL };
    if (!SymEnumSymbols((HANDLE)-1, g_KernelBase, name, EnumSymbolCallback, &ctx) || ctx.address == NULL) {
        Log("[!] resolve %-40s failed: %lu", name, GetLastError());
        return FALSE;
    }
    *pField = ctx.address;
    Log("[+] %-40s = 0x%llX", name, (DWORD64)ctx.address);
    return TRUE;
}

static ULONG64 GetMemberOffset(const char* typeName, const wchar_t* memberName)
{
    SYMBOL_INFO sym = { sizeof(SYMBOL_INFO) };
    sym.MaxNameLen = MAX_SYM_NAME;

    if (!SymGetTypeFromName((HANDLE)-1, g_KernelBase, typeName, &sym)) {
        Log("[!] SymGetTypeFromName %s failed: %lu", typeName, GetLastError());
        return (ULONG64)-1;
    }

    DWORD count = 0;
    if (!SymGetTypeInfo((HANDLE)-1, g_KernelBase, sym.TypeIndex, TI_GET_CHILDRENCOUNT, &count)) {
        Log("[!] TI_GET_CHILDRENCOUNT %s failed", typeName);
        return (ULONG64)-1;
    }

    TI_FINDCHILDREN_PARAMS* p = (TI_FINDCHILDREN_PARAMS*)
        malloc(sizeof(TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG));
    if (!p) return (ULONG64)-1;
    p->Count = count;
    p->Start = 0;

    if (!SymGetTypeInfo((HANDLE)-1, g_KernelBase, sym.TypeIndex, TI_FINDCHILDREN, p)) {
        free(p);
        Log("[!] TI_FINDCHILDREN %s failed", typeName);
        return (ULONG64)-1;
    }

    ULONG64 result = (ULONG64)-1;
    for (DWORD i = 0; i < count; i++) {
        WCHAR* name = NULL;
        if (SymGetTypeInfo((HANDLE)-1, g_KernelBase, p->ChildId[i], TI_GET_SYMNAME, &name)) {
            if (name && _wcsicmp(name, memberName) == 0) {
                ULONG off = 0;
                SymGetTypeInfo((HANDLE)-1, g_KernelBase, p->ChildId[i], TI_GET_OFFSET, &off);
                result = (ULONG64)off;
                LocalFree(name);
                break;
            }
            if (name) LocalFree(name);
        }
    }

    free(p);
    return result;
}

static void SetObjectTypeInitOffsets()
{
    g_SymbolsData.ObjectTypeInit_GenericMapping  = 0x0c;
    g_SymbolsData.ObjectTypeInit_ValidAccessMask = 0x14;
    g_SymbolsData.ObjectTypeInit_CloseProcedure  = 0x40;
    g_SymbolsData.ObjectTypeInit_DeleteProcedure = 0x48;
    Log("[+] _OBJECT_TYPE_INITIALIZER (hardcoded, stable on Win10)");
}

BOOL SymbolLoadAndResolve(PSYMBOLS_DATA out, PDB_LOG_CB log)
{
    g_log = log;
    memset(&g_SymbolsData, 0, sizeof(g_SymbolsData));

    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    g_ZwQuerySystemInformation = (pNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    if (!g_ZwQuerySystemInformation) {
        Log("[!] GetProcAddress NtQuerySystemInformation failed");
        return FALSE;
    }

    if (!LoadKernelSymbols())
        return FALSE;

    Log("--- resolving addresses ---");
    BOOLEAN ok = TRUE;
    for (size_t i = 0; i < sizeof(g_addrTable) / sizeof(g_addrTable[0]); i++) {
        if (!ResolveAddress(g_addrTable[i].pField, g_addrTable[i].name))
            ok = FALSE;
    }

    Log("--- resolving offsets ---");
    for (size_t i = 0; i < sizeof(g_offTable) / sizeof(g_offTable[0]); i++) {
        ULONG64 off = GetMemberOffset(g_offTable[i].type, g_offTable[i].member);
        if (off == (ULONG64)-1) {
            ok = FALSE;
        } else {
            *g_offTable[i].pField = off;
            Log("[+] %s.%ls = 0x%llX", g_offTable[i].type, g_offTable[i].member, off);
        }
    }
    SetObjectTypeInitOffsets();

    if (ok)
        memcpy(out, &g_SymbolsData, sizeof(g_SymbolsData));

    return ok;
}

void SymbolCleanup()
{
    SymCleanup((HANDLE)-1);
    g_log = NULL;
}
