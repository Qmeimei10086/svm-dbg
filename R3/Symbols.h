#pragma once
#include <windows.h>

//
// Must be identical (field order / types) to the driver's DbgHook/Symbols.h.
// R3 fills this struct and sends it to R0 via IOCTL.
//
typedef struct _SYMBOLS_DATA {
	// ---- function addresses ----
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

	// ---- struct member offsets ----
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
} SYMBOLS_DATA, *PSYMBOLS_DATA;
