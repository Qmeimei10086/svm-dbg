#include "dbg.h"
#include "../NPT-Hook/NptHook.h"

extern SYMBOLS_DATA g_SymbolsData;
extern __DbgkpWakeTarget DbgkpWakeTarget;
extern __DbgkpSuppressDbgMsg DbgkpSuppressDbgMsg;
extern __DbgkpMarkProcessPeb DbgkpMarkProcessPeb;
extern __DbgkpSendApiMessage DbgkpSendApiMessage;
extern __DbgkCreateThread OriginalDbgkCreateThread;
extern __DbgkpSendErrorMessage DbgkpSendErrorMessage;
extern __NtTerminateProcess OrignalNtTerminateProcess;
extern __DbgkpSendApiMessageLpc DbgkpSendApiMessageLpc;
extern __PsCaptureExceptionPort PsCaptureExceptionPort;
extern __PsGetNextProcessThread PsGetNextProcessThread;
extern __KiDispatchException OrignalKiDispatchException;
extern __NtCreateUserProcess OrignalNtCreateUserProcess;
extern __DbgkpSectionToFileHandle DbgkpSectionToFileHandle;
extern __DbgkSendSystemDllMessages DbgkSendSystemDllMessages;
extern __DbgkpPostFakeThreadMessages DbgkpPostFakeThreadMessages;
extern __DbgkpPostFakeThreadMessages OriginalDbgkpPostFakeThreadMessages;
extern __DbgkpPostFakeProcessCreateMessages DbgkpPostFakeProcessCreateMessages;
extern KSPIN_LOCK g_DebugLock;
extern DebugInfomation g_Debuginfo;
extern PFAST_MUTEX DbgkpProcessDebugPortMutex;

__DbgkpQueueMessage OriginalDbgkpQueueMessage = NULL;
__NtCreateDebugObject OriginalNtCreateDebugObject = NULL;
__DbgkForwardException OriginalDbgkForwardException = NULL;
__NtDebugActiveProcess OriginalNtDebugActiveProcess = NULL;
__DbgkMapViewOfSection OriginalDbgkMapViewOfSection = NULL;
__DbgkUnMapViewOfSection OriginalDbgkUnMapViewOfSection = NULL;
__DbgkpSetProcessDebugObject OriginalDbgkpSetProcessDebugObject = NULL;

POBJECT_TYPE* g_DbgkDebugObjectType;

#define MAX_HOOK_COUNT 32
static PVOID g_hookedOrigins[MAX_HOOK_COUNT] = { 0 };
static ULONG g_hookCount = 0;

// Install an NPT hook: get the caller trampoline, then add the hook.
static VOID HookFunction(PVOID pFunc, PVOID hookAddress, PVOID* outAddress)
{
	if (!pFunc)
		return;

	*outAddress = NptHookGetFunctionCaller(pFunc);
	NptHookAdd(pFunc, hookAddress);

	if (g_hookCount < MAX_HOOK_COUNT)
		g_hookedOrigins[g_hookCount++] = pFunc;
}

// Replace the global DbgkDebugObjectType with a fake type ("YCData"),
// so anti-debug (NtQueryObject ObjectTypeInformation) cannot see "DebugObject".
BOOLEAN HookDbgkDebugObjectType()
{
	UNICODE_STRING ObjectTypeName;

	g_DbgkDebugObjectType = (POBJECT_TYPE*)g_SymbolsData.DbgkDebugObjectType;
	if (g_DbgkDebugObjectType == 0)
		return FALSE;

	RtlInitUnicodeString(&ObjectTypeName, L"YCData");
	PUCHAR pTypeInfo = (PUCHAR)&(*g_DbgkDebugObjectType)->TypeInfo;
	USHORT Length = *(PUSHORT)pTypeInfo;
	PUCHAR pInit = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, Length, 'YC');
	if (!pInit)
		return FALSE;
	RtlCopyMemory(pInit, pTypeInfo, Length);
	*(PVOID*)(pInit + g_SymbolsData.ObjectTypeInit_DeleteProcedure) = NULL;
	*(PVOID*)(pInit + g_SymbolsData.ObjectTypeInit_CloseProcedure) = NULL;
	*(PULONG)(pInit + g_SymbolsData.ObjectTypeInit_GenericMapping + 0x00) = 0x00020001;
	*(PULONG)(pInit + g_SymbolsData.ObjectTypeInit_GenericMapping + 0x04) = 0x00020002;
	*(PULONG)(pInit + g_SymbolsData.ObjectTypeInit_GenericMapping + 0x08) = 0x00120000;
	*(PULONG)(pInit + g_SymbolsData.ObjectTypeInit_GenericMapping + 0x0c) = 0x001f000f;
	*(PULONG)(pInit + g_SymbolsData.ObjectTypeInit_ValidAccessMask) = 0x001f000f;

	NTSTATUS status = ObCreateObjectType(&ObjectTypeName, pInit, NULL, (PVOID*)g_DbgkDebugObjectType);
	ExFreePoolWithTag(pInit, 'YC');
	if (!NT_SUCCESS(status))
	{
		if (status == STATUS_OBJECT_NAME_COLLISION)
		{
			POBJECT_TYPE* ObTypeIndexTable = (POBJECT_TYPE*)g_SymbolsData.ObTypeIndexTable;
			if (!ObTypeIndexTable)
				return FALSE;
			ULONG Index = 2;
			while (ObTypeIndexTable[Index])
			{
				if (&ObTypeIndexTable[Index]->Name && ObTypeIndexTable[Index]->Name.Buffer &&
					RtlCompareUnicodeString(&ObTypeIndexTable[Index]->Name, &ObjectTypeName, FALSE) == 0)
				{
					*g_DbgkDebugObjectType = ObTypeIndexTable[Index];
					return TRUE;
				}
				Index++;
			}
		}
	}
	return TRUE;
}

BOOLEAN DbgInit(ULONG Flags)
{
	// Resolve helper function addresses from the symbol table.
	DbgkpWakeTarget = (__DbgkpWakeTarget)g_SymbolsData.DbgkpWakeTarget;
	DbgkpSuppressDbgMsg = (__DbgkpSuppressDbgMsg)g_SymbolsData.DbgkpSuppressDbgMsg;
	DbgkpSendApiMessage = (__DbgkpSendApiMessage)g_SymbolsData.DbgkpSendApiMessage;
	DbgkpMarkProcessPeb = (__DbgkpMarkProcessPeb)g_SymbolsData.DbgkpMarkProcessPeb;
	DbgkpSendErrorMessage = (__DbgkpSendErrorMessage)g_SymbolsData.DbgkpSendErrorMessage;
	PsGetNextProcessThread = (__PsGetNextProcessThread)g_SymbolsData.PsGetNextProcessThread;
	DbgkpSendApiMessageLpc = (__DbgkpSendApiMessageLpc)g_SymbolsData.DbgkpSendApiMessageLpc;
	PsCaptureExceptionPort = (__PsCaptureExceptionPort)g_SymbolsData.PsCaptureExceptionPort;
	DbgkpSectionToFileHandle = (__DbgkpSectionToFileHandle)g_SymbolsData.DbgkpSectionToFileHandle;
	DbgkSendSystemDllMessages = (__DbgkSendSystemDllMessages)g_SymbolsData.DbgkSendSystemDllMessages;
	DbgkpPostFakeThreadMessages = (__DbgkpPostFakeThreadMessages)g_SymbolsData.DbgkpPostFakeThreadMessages;
	DbgkpPostFakeProcessCreateMessages = (__DbgkpPostFakeProcessCreateMessages)g_SymbolsData.DbgkpPostFakeProcessCreateMessages;
	DbgkpProcessDebugPortMutex = (PFAST_MUTEX)g_SymbolsData.DbgkpProcessDebugPortMutex;

	if (!DbgkpWakeTarget ||
		!DbgkpSendApiMessage ||
		!DbgkpMarkProcessPeb ||
		!DbgkpPostFakeThreadMessages ||
		!DbgkpPostFakeProcessCreateMessages ||
		!g_SymbolsData.NtCreateUserProcess ||
		!g_SymbolsData.DbgkCreateThread ||
		!g_SymbolsData.DbgkMapViewOfSection ||
		!g_SymbolsData.DbgkUnMapViewOfSection ||
		!g_SymbolsData.DbgkForwardException ||
		!g_SymbolsData.KiDispatchException ||
		!DbgkpProcessDebugPortMutex)
	{
		DbgPrintEx(77, 0, "[DbgHook] required debug symbol is missing\n");
		return FALSE;
	}

	InitializeListHead(&g_Debuginfo.List);
	KeInitializeSpinLock(&g_DebugLock);

	// Install hooks required by both Attach and CreateProcess debugging.
	
	HookFunction(g_SymbolsData.DbgkpQueueMessage, DbgkpQueueMessage, (PVOID*)&OriginalDbgkpQueueMessage);
	//HookFunction(g_SymbolsData.DbgkpPostFakeThreadMessages, DbgkpPostFakeThreadMessagesHook, (PVOID*)&OriginalDbgkpPostFakeThreadMessages);
	HookFunction(g_SymbolsData.NtTerminateProcess, NtTerminateProcess, (PVOID*)&OrignalNtTerminateProcess);
	HookFunction(g_SymbolsData.KiDispatchException, KiDispatchException, (PVOID*)&OrignalKiDispatchException);
	HookFunction(g_SymbolsData.NtCreateDebugObject, NtCreateDebugObject, (PVOID*)&OriginalNtCreateDebugObject);
	HookFunction(g_SymbolsData.NtDebugActiveProcess, NtDebugActiveProcess, (PVOID*)&OriginalNtDebugActiveProcess);
	HookFunction(g_SymbolsData.DbgkForwardException, DbgkForwardException, (PVOID*)&OriginalDbgkForwardException);
	HookFunction(g_SymbolsData.DbgkpSetProcessDebugObject, DbgkpSetProcessDebugObject, (PVOID*)&OriginalDbgkpSetProcessDebugObject);

	// CreateProcess-class hooks are only installed when the user opts in
	// (勾选"重建创建调试"). They are the hottest / least stable hooks.
	if (Flags & DBG_FLAG_REBUILD_CREATE_DEBUG)
	{
		HookFunction(g_SymbolsData.NtCreateUserProcess, NtCreateUserProcess, (PVOID*)&OrignalNtCreateUserProcess);
		HookFunction(g_SymbolsData.DbgkMapViewOfSection, DbgkMapViewOfSection, (PVOID*)&OriginalDbgkMapViewOfSection);
		HookFunction(g_SymbolsData.DbgkUnMapViewOfSection, DbgkUnMapViewOfSection, (PVOID*)&OriginalDbgkUnMapViewOfSection);
		HookFunction(g_SymbolsData.DbgkCreateThread, DbgkCreateThread, (PVOID*)&OriginalDbgkCreateThread);
	}

	if (!HookDbgkDebugObjectType())
		return FALSE;

	return TRUE;
}

BOOLEAN UnHookFuncs()
{
	for (ULONG i = 0; i < g_hookCount; i++)
	{
		NptHookRemove(g_hookedOrigins[i]);
		NptHookRemoveFunctionCaller(g_hookedOrigins[i]);
	}
	g_hookCount = 0;

	NptHookUninitialize();
	DbgPrintEx(77, 0, "[DbgHook] Devirtualized the system.\n");
	DbgPrintEx(77, 0, "[DbgHook] Driver unloaded.\n");
	return TRUE;
}
