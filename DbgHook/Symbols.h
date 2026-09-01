#pragma once
#include <ntddk.h>

//
// R3 通过符号解析（dbghelp + symsrv）得到的地址表和偏移表，通过 IOCTL 一次性传给 R0。
// 字段顺序必须与 R3 的 Symbols.h 保持一致。
//
typedef struct _SYMBOLS_DATA {
	// ---- 函数地址 ----
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

	// ---- 结构体成员偏移（由 PDB 解析，避免写死） ----
	ULONG64 Process_DebugPort;                       // _EPROCESS.DebugPort
	ULONG64 Process_RundownProtect;                  // _EPROCESS.RundownProtect
	ULONG64 Process_Flags;                           // _EPROCESS.Flags
	ULONG64 Process_SectionObject;                   // _EPROCESS.SectionObject
	ULONG64 Process_SectionBaseAddress;              // _EPROCESS.SectionBaseAddress
	ULONG64 Thread_CrossThreadFlags;                 // _ETHREAD.CrossThreadFlags
	ULONG64 Thread_RundownProtect;                   // _ETHREAD.RundownProtect
	ULONG64 Thread_Win32StartAddress;                // _ETHREAD.Win32StartAddress
	ULONG64 ObjectTypeInit_GenericMapping;           // _OBJECT_TYPE_INITIALIZER.GenericMapping
	ULONG64 ObjectTypeInit_ValidAccessMask;          // _OBJECT_TYPE_INITIALIZER.ValidAccessMask
	ULONG64 ObjectTypeInit_CloseProcedure;           // _OBJECT_TYPE_INITIALIZER.CloseProcedure
	ULONG64 ObjectTypeInit_DeleteProcedure;          // _OBJECT_TYPE_INITIALIZER.DeleteProcedure

	// 控制位：DBG_FLAG_REBUILD_CREATE_DEBUG = 勾选"重建创建调试"
	ULONG Flags;
} SYMBOLS_DATA, *PSYMBOLS_DATA;

// 26 个 PVOID + 12 个 ULONG64 = 304 字节（Flags 之前的部分）
#define SYMBOLS_DATA_BASE_SIZE    (26 * sizeof(PVOID) + 12 * sizeof(ULONG64))

#define DBG_FLAG_REBUILD_CREATE_DEBUG 0x00000001
