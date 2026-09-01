#pragma once

#include <windows.h>

typedef void(__cdecl* PDB_LOG_CB)(const char* fmt, ...);

typedef struct _SYMBOLS_DATA {
    PVOID NtCreateDebugObject;
    PVOID PsGetNextProcessThread;
    PVOID DbgkpPostFakeThreadMessages;
    PVOID DbgkpWakeTarget;
    PVOID DbgkpSetProcessDebugObject;
    PVOID DbgkCreateThread;
    PVOID DbgkpQueueMessage;
    PVOID PsCaptureExceptionPort;
    PVOID DbgkpSendApiMessage;
    PVOID DbgkpSendApiMessageLpc;
    PVOID DbgkpSendErrorMessage;
    PVOID DbgkForwardException;
    PVOID DbgkpSuppressDbgMsg;
    PVOID DbgkpSectionToFileHandle;
    PVOID DbgkUnMapViewOfSection;
    PVOID DbgkpPostFakeProcessCreateMessages;
    PVOID NtDebugActiveProcess;
    PVOID DbgkpMarkProcessPeb;
    PVOID KiDispatchException;
    PVOID NtCreateUserProcess;
    PVOID DbgkDebugObjectType;
    PVOID ObTypeIndexTable;
    PVOID NtTerminateProcess;
    PVOID DbgkMapViewOfSection;
    PVOID DbgkSendSystemDllMessages;
    PVOID DbgkpProcessDebugPortMutex;

    ULONG64 Process_DebugPort;
    ULONG64 Process_RundownProtect;
    ULONG64 Process_Flags;
    ULONG64 Process_SectionObject;
    ULONG64 Process_SectionBaseAddress;
    ULONG64 Thread_CrossThreadFlags;
    ULONG64 Thread_RundownProtect;
    ULONG64 Thread_Win32StartAddress;
    ULONG64 ObjectTypeInit_GenericMapping;
    ULONG64 ObjectTypeInit_ValidAccessMask;
    ULONG64 ObjectTypeInit_CloseProcedure;
    ULONG64 ObjectTypeInit_DeleteProcedure;

    ULONG Flags;
} SYMBOLS_DATA, *PSYMBOLS_DATA;

// 26 PVOID fields + 12 ULONG64 fields = 304 bytes (portion before Flags)
#define SYMBOLS_DATA_BASE_SIZE    (26 * sizeof(PVOID) + 12 * sizeof(ULONG64))

#define DBG_FLAG_REBUILD_CREATE_DEBUG 0x00000001
