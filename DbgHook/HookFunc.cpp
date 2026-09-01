#include"HookFunc.h"

extern SYMBOLS_DATA g_SymbolsData;
extern POBJECT_TYPE* g_DbgkDebugObjectType;

PFAST_MUTEX DbgkpProcessDebugPortMutex;

__DbgkpWakeTarget DbgkpWakeTarget = NULL;
__DbgkpSendApiMessage DbgkpSendApiMessage = NULL;
__DbgkpSuppressDbgMsg DbgkpSuppressDbgMsg = NULL;
__DbgkpMarkProcessPeb DbgkpMarkProcessPeb = NULL;
__DbgkCreateThread OriginalDbgkCreateThread = NULL;
__DbgkpSendErrorMessage DbgkpSendErrorMessage = NULL;
__NtTerminateProcess OrignalNtTerminateProcess = NULL;
__DbgkpSendApiMessageLpc DbgkpSendApiMessageLpc = NULL;
__PsCaptureExceptionPort PsCaptureExceptionPort = NULL;
__KiDispatchException OrignalKiDispatchException = NULL;
__NtCreateUserProcess OrignalNtCreateUserProcess = NULL;
__PsGetNextProcessThread  PsGetNextProcessThread = NULL;
__DbgkpSectionToFileHandle DbgkpSectionToFileHandle = NULL;
__DbgkSendSystemDllMessages DbgkSendSystemDllMessages = NULL;
__DbgkpPostFakeThreadMessages  DbgkpPostFakeThreadMessages = NULL;
__DbgkpPostFakeThreadMessages  OriginalDbgkpPostFakeThreadMessages = NULL;
__DbgkpPostFakeProcessCreateMessages DbgkpPostFakeProcessCreateMessages = NULL;

KSPIN_LOCK g_DebugLock = {};
DebugInfomation g_Debuginfo = { 0 };

PVOID GetThread_CrossThreadFlags(PETHREAD EThread)
{
	return (PUCHAR)EThread + g_SymbolsData.Thread_CrossThreadFlags;
}
PVOID GetThread_RundownProtect(PETHREAD EThread)
{
	return (PUCHAR)EThread + g_SymbolsData.Thread_RundownProtect;
}
PVOID GetProcess_DebugPort(PEPROCESS EProcess)
{
	return (PUCHAR)EProcess + g_SymbolsData.Process_DebugPort;
}
PVOID GetProcess_RundownProtect(PEPROCESS EProcess)
{
	return (PUCHAR)EProcess + g_SymbolsData.Process_RundownProtect;
}
PVOID GetProcess_ProcessFlags(PEPROCESS EProcess)
{
	return (PUCHAR)EProcess + g_SymbolsData.Process_Flags;
}
PVOID GetProcess_SectionObject(PEPROCESS EProcess)
{
	return (PUCHAR)EProcess + g_SymbolsData.Process_SectionObject;
}
PVOID GetProcess_SectionBaseAddress(PEPROCESS EProcess)
{
	return (PUCHAR)EProcess + g_SymbolsData.Process_SectionBaseAddress;
}
PVOID GetThread_StartAddress(PETHREAD EThread)
{
	return (PUCHAR)EThread + g_SymbolsData.Thread_Win32StartAddress;
}

NTSTATUS  NtCreateDebugObject(
	PHANDLE DebugObjectHandle,
	ACCESS_MASK DesiredAccess,
	POBJECT_ATTRIBUTES ObjectAttributes,
	ULONG Flags)
{
	DbgPrintEx(77, 0, "[DbgHook] NtCreateDebugObject enter pid=%lu\n", (ULONG)PsGetCurrentProcessId());

	NTSTATUS status;
	HANDLE Handle;
	PDEBUG_OBJECT DebugObject;
	KPROCESSOR_MODE	PreviousMode;

	PreviousMode = ExGetPreviousMode();

	_try{
		if (PreviousMode != KernelMode) {
			ProbeForWrite(DebugObjectHandle,sizeof(HANDLE),sizeof(UCHAR));
		}
		*DebugObjectHandle = NULL;

	} _except(ExSystemExceptionFilter()) {
		return GetExceptionCode();
	}

	if (Flags & ~DEBUG_KILL_ON_CLOSE) {
		return STATUS_INVALID_PARAMETER;
	}

	status = ObCreateObject(
		PreviousMode,
		*g_DbgkDebugObjectType,
		ObjectAttributes,
		PreviousMode,
		NULL,
		sizeof(DEBUG_OBJECT),
		0,
		0,
		(PVOID*)& DebugObject);

	DbgPrintEx(77, 0, "[DbgHook] ObCreateObject status=0x%08X\n", status);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	ExInitializeFastMutex(&DebugObject->Mutex);
	InitializeListHead(&DebugObject->EventList);
	KeInitializeEvent(&DebugObject->EventsPresent, NotificationEvent, FALSE);

	if (Flags & DEBUG_KILL_ON_CLOSE) {
		DebugObject->Flags = DEBUG_OBJECT_KILL_ON_CLOSE;
	}
	else {
		DebugObject->Flags = 0;
	}

	status = ObInsertObject(
		DebugObject,
		NULL,
		DesiredAccess,
		0,
		NULL,
		&Handle);
	DbgPrintEx(77, 0, "[DbgHook] ObInsertObject status=0x%08X\n", status);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	_try{
		*DebugObjectHandle = Handle;
	} _except(ExSystemExceptionFilter()) {
		status = GetExceptionCode();
		return status;
	}

	PDebugInfomation pDebuginfo = (PDebugInfomation)ExAllocatePoolWithTag(NonPagedPool, sizeof(DebugInfomation), 'YC');
	if (pDebuginfo)
	{
		memset(pDebuginfo, 0, sizeof(DebugInfomation));

		pDebuginfo->SourceProcessId = PsGetCurrentProcessId();
		pDebuginfo->DebugObject = DebugObject;

		KIRQL OldIrql = { 0 };
		KeAcquireSpinLock(&g_DebugLock, &OldIrql);
		InsertTailList(&g_Debuginfo.List, &pDebuginfo->List);
		KeReleaseSpinLock(&g_DebugLock, OldIrql);
	}
	DbgPrintEx(77, 0, "[DbgHook] NtCreateDebugObject done\n");
	return status;
}


NTSTATUS DbgkpSetProcessDebugObject(
	PEPROCESS Process,
	PDEBUG_OBJECT DebugObject,
	NTSTATUS MsgStatus,
	PETHREAD LastThread)
{
	NTSTATUS Status;
	PETHREAD ThisThread;
	LIST_ENTRY TempList;
	PLIST_ENTRY Entry;
	PDEBUG_EVENT DebugEvent;
	BOOLEAN First;
	PETHREAD Thread;
	BOOLEAN GlobalHeld;
	PETHREAD FirstThread = NULL;


	ThisThread = (PETHREAD)PsGetCurrentThread();
	InitializeListHead(&TempList);
	First = TRUE;
	GlobalHeld = FALSE;
	if (!NT_SUCCESS(MsgStatus)) {
		LastThread = NULL;
		Status = MsgStatus;
	}
	else {
		Status = STATUS_SUCCESS;
	}

	if (NT_SUCCESS(Status)) {
		while (TRUE) {

			////��������DebugPort����������
			//PVOID DebugPort__ = GetProcess_DebugPort(Process);
			//*(ULONG64 *)(DebugPort__) = (ULONG64)DebugObject;
			ExAcquireFastMutex(DbgkpProcessDebugPortMutex);

			GlobalHeld = TRUE;
			if (LastThread != NULL) {
				ObfReferenceObject(LastThread);
			}
			Thread = (PETHREAD)PsGetNextProcessThread((PEPROCESS)Process, (PETHREAD)LastThread);
			if (Thread != NULL) {

				ExReleaseFastMutex(DbgkpProcessDebugPortMutex);

				GlobalHeld = FALSE;
				if (LastThread != NULL) {
					ObfDereferenceObject(LastThread);
				}
				Status = DbgkpPostFakeThreadMessages(
					Process,
					DebugObject,
					Thread,
					&FirstThread,
					&LastThread);
				if (!NT_SUCCESS(Status)) {
					LastThread = NULL;
					break;
				}
				if (FirstThread != NULL) {
					ObfDereferenceObject(FirstThread);
					FirstThread = NULL;
				}
			}
			else {
				break;
			}
		}
	}

	// Keep the real DebugPort hidden. DbgkpQueueMessage uses g_Debuginfo to
	// route normal events while the target process remains non-debuggable to
	// callers that inspect EPROCESS.
	ExAcquireFastMutex(&DebugObject->Mutex);
	if (NT_SUCCESS(Status)) {
		if ((DebugObject->Flags & DEBUG_OBJECT_DELETE_PENDING) == 0) {
			ObfReferenceObject(DebugObject);
		}
		else {
			Status = STATUS_DEBUGGER_INACTIVE;
		}
	}

	for (Entry = DebugObject->EventList.Flink; Entry != &DebugObject->EventList;) {
		DebugEvent = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);
		Entry = Entry->Flink;

		if ((DebugEvent->Flags & DEBUG_EVENT_INACTIVE) != 0 && DebugEvent->BackoutThread == (PETHREAD)ThisThread) {
			Thread = DebugEvent->Thread;

			if (NT_SUCCESS(Status)) {
				if ((DebugEvent->Flags & DEBUG_EVENT_PROTECT_FAILED) != 0) {
					PVOID CrossThreadFlags = GetThread_CrossThreadFlags(Thread);
					RtlInterlockedSetBitsDiscardReturn(CrossThreadFlags, 0x100);
					RemoveEntryList(&DebugEvent->EventList);
					InsertTailList(&TempList, &DebugEvent->EventList);
				}
				else {
					if (First) {
						DebugEvent->Flags &= ~DEBUG_EVENT_INACTIVE;
						KeSetEvent(&DebugObject->EventsPresent, 0, FALSE);
						First = FALSE;
					}
					DebugEvent->BackoutThread = NULL;
					PVOID CrossThreadFlags = GetThread_CrossThreadFlags(Thread);
					RtlInterlockedSetBitsDiscardReturn(CrossThreadFlags, 0x80);
				}
			}
			else {
				RemoveEntryList(&DebugEvent->EventList);
				InsertTailList(&TempList, &DebugEvent->EventList);
			}

			if (DebugEvent->Flags & DEBUG_EVENT_RELEASE) {
				DebugEvent->Flags &= ~DEBUG_EVENT_RELEASE;
				PVOID RundownProtect = GetThread_RundownProtect(Thread);
				ExReleaseRundownProtection((PEX_RUNDOWN_REF)RundownProtect);
			}

		}
	}

	ExReleaseFastMutex(&DebugObject->Mutex);

	if (GlobalHeld)
	{
		ExReleaseFastMutex(DbgkpProcessDebugPortMutex);
	}

	if (LastThread != NULL) {
		ObDereferenceObject(LastThread);
	}

	while (!IsListEmpty(&TempList)) {
		Entry = RemoveHeadList(&TempList);
		DebugEvent = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);
		DbgkpWakeTarget(DebugEvent);
	}

	//������������BeingDebugged��
	//if (NT_SUCCESS(Status)) {
	// DbgkpMarkProcessPeb(Process);
	//}

	return Status;
}

NTSTATUS DbgkpPostFakeThreadMessagesHook(
	PEPROCESS Process,
	PDEBUG_OBJECT DebugObject,
	PETHREAD StartThread,
	PETHREAD* pFirstThread,
	PETHREAD* pLastThread)
{
	DbgPrintEx(77, 0,
		"[DbgHook] DbgkpPostFakeThreadMessages enter Process=%p DebugObject=%p StartThread=%p\n",
		Process, DebugObject, StartThread);

	NTSTATUS Status = OriginalDbgkpPostFakeThreadMessages(
		Process,
		DebugObject,
		StartThread,
		pFirstThread,
		pLastThread);

	DbgPrintEx(77, 0,
		"[DbgHook] DbgkpPostFakeThreadMessages return status=0x%08X FirstThread=%p LastThread=%p\n",
		Status,
		pFirstThread ? *pFirstThread : NULL,
		pLastThread ? *pLastThread : NULL);
	return Status;
}


VOID  DbgkCreateThread(
	PETHREAD Thread)
{
	PVOID Port;
	DBGKM_APIMSG m;
	PDBGKM_CREATE_THREAD CreateThreadArgs;
	PDBGKM_CREATE_PROCESS CreateProcessArgs;
	PEPROCESS Process = PsGetCurrentProcess();
	HANDLE ProcessId = PsGetCurrentProcessId();
	PDBGKM_LOAD_DLL LoadDllArgs;
	NTSTATUS Status;
	OBJECT_ATTRIBUTES Obja;
	IO_STATUS_BLOCK IoStatusBlock;
	PIMAGE_NT_HEADERS NtHeaders;
	PTEB Teb;

	RtlZeroMemory(&m, sizeof(m));

	BOOLEAN isDebug = FALSE;
	KIRQL OldIrql = { 0 };
	KeAcquireSpinLock(&g_DebugLock, &OldIrql);
	for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
	{
		PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
		if (pDebuginfo->TargetProcessId == PsGetCurrentProcessId())
		{
			isDebug = TRUE;
			break;
		}
	}
	KeReleaseSpinLock(&g_DebugLock, OldIrql);

	if (isDebug)
	{
		PVOID ProFlag = GetProcess_ProcessFlags(Process);
		ULONG OldFlags = RtlInterlockedSetBits(ProFlag, 0x400001);	//RtlInterlockedSetBits(&Process->Flags, 0x400001);֮ǰ���bug��win7�ͻ���֣����Ұ���
		if ((OldFlags & PS_PROCESS_FLAGS_CREATE_REPORTED) == 0)
		{
			CreateThreadArgs = &m.u.CreateProcessInfo.InitialThread;
			CreateThreadArgs->SubSystemKey = 0;

			CreateProcessArgs = &m.u.CreateProcessInfo;
			CreateProcessArgs->SubSystemKey = 0;
			CreateProcessArgs->FileHandle = DbgkpSectionToFileHandle((PVOID) * (PULONG64)GetProcess_SectionObject(Process));
			CreateProcessArgs->BaseOfImage = (PVOID) * (PULONG64)GetProcess_SectionBaseAddress(Process);
			CreateThreadArgs->StartAddress = NULL;
			CreateProcessArgs->DebugInfoFileOffset = 0;
			CreateProcessArgs->DebugInfoSize = 0;

			__try
			{
				NtHeaders = RtlImageNtHeader((PVOID) * (PULONG64)GetProcess_SectionBaseAddress(Process));
				if (NtHeaders)
				{
					if (PsGetProcessWow64Process(Process) != NULL)
					{
						CreateThreadArgs->StartAddress = UlongToPtr(DBGKP_FIELD_FROM_IMAGE_OPTIONAL_HEADER((PIMAGE_NT_HEADERS32)NtHeaders, ImageBase) + DBGKP_FIELD_FROM_IMAGE_OPTIONAL_HEADER((PIMAGE_NT_HEADERS32)NtHeaders, AddressOfEntryPoint));
					}
					else {
						CreateThreadArgs->StartAddress = (PVOID)(DBGKP_FIELD_FROM_IMAGE_OPTIONAL_HEADER(NtHeaders, ImageBase) + DBGKP_FIELD_FROM_IMAGE_OPTIONAL_HEADER(NtHeaders, AddressOfEntryPoint));
					}
					CreateProcessArgs->DebugInfoFileOffset = NtHeaders->FileHeader.PointerToSymbolTable;
					CreateProcessArgs->DebugInfoSize = NtHeaders->FileHeader.NumberOfSymbols;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				CreateThreadArgs->StartAddress = NULL;
				CreateProcessArgs->DebugInfoFileOffset = 0;
				CreateProcessArgs->DebugInfoSize = 0;
			}

			m.h.u1.Length = 0x600038;
			m.h.u2.ZeroInit = 8;
			m.ApiNumber = DbgKmCreateProcessApi;

#ifdef WIN7
			DbgkpSendApiMessage(FALSE, &m);
#else
			DbgkpSendApiMessage(Process, FALSE, &m);
#endif

			if (CreateProcessArgs->FileHandle != NULL) {
				ObCloseHandle(CreateProcessArgs->FileHandle, KernelMode);
			}
			DbgkSendSystemDllMessages(0, 0, &m);
		}
		else
		{
			CreateThreadArgs = &m.u.CreateThread;
			CreateThreadArgs->SubSystemKey = 0;
			CreateThreadArgs->StartAddress = (PVOID) * (PULONG64)GetThread_StartAddress(Thread);

			m.h.u1.Length = 0x400018;
			m.h.u2.ZeroInit = 8;
			m.ApiNumber = DbgKmCreateThreadApi;

#ifdef WIN7
			DbgkpSendApiMessage(TRUE, &m);
#else
			DbgkpSendApiMessage(Process, TRUE, &m);
#endif

		}
	}

	OriginalDbgkCreateThread(Thread);
}


NTSTATUS DbgkpQueueMessage(
	PEPROCESS Process,
	PETHREAD Thread,
	PDBGKM_APIMSG ApiMsg,
	ULONG Flags,
	PDEBUG_OBJECT TargetDebugObject)
{
	DbgPrintEx(77, 0,
		"[DbgHook] DbgkpQueueMessage enter pid=%lu api=%lu flags=0x%08X explicit=%p\n",
		(ULONG)PsGetProcessId(Process),
		(ULONG)ApiMsg->ApiNumber,
		Flags,
		TargetDebugObject);
	PDEBUG_EVENT DebugEvent;
	DEBUG_EVENT StaticDebugEvent;
	PDEBUG_OBJECT DebugObject = NULL;
	NTSTATUS Status;

	RtlZeroMemory(&StaticDebugEvent, sizeof(StaticDebugEvent));

	if (Flags & DEBUG_EVENT_NOWAIT)
	{
		DebugEvent = (PDEBUG_EVENT)ExAllocatePoolWithQuotaTag((POOL_TYPE)(NonPagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE), sizeof(DEBUG_EVENT), 'EgbD');//sizeof (DEBUG_EVENT)=0x168
		if (!DebugEvent)
		{
			return  STATUS_INSUFFICIENT_RESOURCES;
		}

		DebugEvent->Flags = Flags | DEBUG_EVENT_INACTIVE;//offset: 0x13
		ObReferenceObject(Thread);
		ObReferenceObject(Process);
		DebugObject = TargetDebugObject;
		DebugEvent->BackoutThread = PsGetCurrentThread();

		// Create-process notifications are commonly queued with NOWAIT before
		// NtCreateUserProcess returns. In that window the explicit object and
		// the PID map may both be unavailable, while the native DebugPort still
		// temporarily identifies the debug object.
		if (DebugObject == NULL)
		{
			PVOID DebugPortAddress = GetProcess_DebugPort(Process);
			if (DebugPortAddress != NULL)
			{
				DebugObject = *(PDEBUG_OBJECT*)DebugPortAddress;
				if (DebugObject != NULL)
				{
					DbgPrintEx(77, 0,
						"[DbgHook] DbgkpQueueMessage NOWAIT using transient DebugPort pid=%lu object=%p\n",
						(ULONG)PsGetProcessId(Process), DebugObject);
				}
			}
		}

	}
	else
	{
		DebugEvent = &StaticDebugEvent;
		DebugEvent->Flags = Flags;
		// The kernel caller already resolved the object for this event.  This is
		// important during attach, before the PID mapping is fully observable.
		DebugObject = TargetDebugObject;

		// During NtCreateUserProcess, the process DebugPort is populated by the
		// native path before the first create event is queued.  The user-process
		// wrapper cannot establish the PID mapping yet because it has not returned.
		// Use that transient native value only for routing this event; the wrapper
		// still clears it after process creation completes.
		if (DebugObject == NULL)
		{
			PVOID DebugPortAddress = GetProcess_DebugPort(Process);
			if (DebugPortAddress != NULL)
			{
				DebugObject = *(PDEBUG_OBJECT*)DebugPortAddress;
				if (DebugObject != NULL)
				{
					DbgPrintEx(77, 0,
						"[DbgHook] DbgkpQueueMessage using transient Process->DebugPort pid=%lu object=%p\n",
						(ULONG)PsGetProcessId(Process), DebugObject);
				}
			}
		}

		ExAcquireFastMutex(DbgkpProcessDebugPortMutex);

		if (DebugObject == NULL)
		{
			KIRQL OldIrql = { 0 };
			KeAcquireSpinLock(&g_DebugLock, &OldIrql);
			for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
			{
				PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
				if (pDebuginfo->TargetProcessId == PsGetProcessId(Process))
				{
					DebugObject = pDebuginfo->DebugObject;
					break;
				}
			}

			// During CreateProcess, the first debug events are emitted before
			// NtCreateUserProcess returns, so TargetProcessId is not known yet.
			// The creator PID is the debugger PID for this synchronous path.
			if (DebugObject == NULL &&
				PsGetProcessId(Process) != PsGetCurrentProcessId())
			{
				for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
				{
					PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
					if (pDebuginfo->SourceProcessId == PsGetCurrentProcessId())
					{
						DebugObject = pDebuginfo->DebugObject;
						pDebuginfo->TargetProcessId = PsGetProcessId(Process);
						DbgPrintEx(77, 0,
							"[DbgHook] DbgkpQueueMessage creator fallback source=%lu target=%lu object=%p\n",
							(ULONG)PsGetCurrentProcessId(),
							(ULONG)PsGetProcessId(Process),
							DebugObject);
						break;
					}
				}
			}
			KeReleaseSpinLock(&g_DebugLock, OldIrql);
		}

		PVOID CrossThreadFlags = GetThread_CrossThreadFlags(Thread);
		if (ApiMsg->ApiNumber == DbgKmCreateThreadApi || ApiMsg->ApiNumber == DbgKmCreateProcessApi) {
			if (*(PULONG)(CrossThreadFlags)& PS_CROSS_THREAD_FLAGS_SKIP_CREATION_MSG) {
				DebugObject = NULL;
			}
		}

		if (ApiMsg->ApiNumber == DbgKmExitThreadApi || ApiMsg->ApiNumber == DbgKmExitProcessApi) {
			if (*(PULONG)(CrossThreadFlags)& PS_CROSS_THREAD_FLAGS_SKIP_TERMINATION_MSG) {
				DebugObject = NULL;
			}
		}
	}
	KeInitializeEvent(&DebugEvent->ContinueEvent, SynchronizationEvent, FALSE);

	DebugEvent->Process = Process;
	DebugEvent->Thread = Thread;
	DebugEvent->ApiMsg = *ApiMsg;
	DebugEvent->ClientId.UniqueProcess = PsGetThreadProcessId(Thread);
	DebugEvent->ClientId.UniqueThread = PsGetThreadId(Thread);


	//KIRQL irql = KeGetCurrentIrql();//win7 ������ܻᱨirql bsod���������ֱ�ӷ���
		if (DebugObject == NULL/* || irql >= APC_LEVEL*/)
	{
			DbgPrintEx(77, 0,
				"[DbgHook] DbgkpQueueMessage no debug object pid=%lu api=%lu flags=0x%08X\n",
				(ULONG)PsGetProcessId(Process),
				(ULONG)ApiMsg->ApiNumber,
				Flags);
			Status = STATUS_PORT_NOT_SET;
	}
	else
	{
		ExAcquireFastMutex(&DebugObject->Mutex);
		if ((DebugObject->Flags & DEBUG_OBJECT_DELETE_PENDING) == 0) {
			InsertTailList(&DebugObject->EventList, &DebugEvent->EventList);

			if ((Flags & DEBUG_EVENT_NOWAIT) == 0) {
				KeSetEvent(&DebugObject->EventsPresent, 0, FALSE);
			}
			Status = STATUS_SUCCESS;
			DbgPrintEx(77, 0,
				"[DbgHook] DbgkpQueueMessage queued pid=%lu api=%lu flags=0x%08X nowait=%d\n",
				(ULONG)PsGetProcessId(Process),
				(ULONG)ApiMsg->ApiNumber,
				Flags,
				(Flags & DEBUG_EVENT_NOWAIT) != 0);
		}
		else
		{
			Status = STATUS_DEBUGGER_INACTIVE;
		}
		ExReleaseFastMutex(&DebugObject->Mutex);
	}

	if ((Flags & DEBUG_EVENT_NOWAIT) == 0) {
		ExReleaseFastMutex(DbgkpProcessDebugPortMutex);

		if (NT_SUCCESS(Status)) {
			KeWaitForSingleObject(
				&DebugEvent->ContinueEvent,
				Executive,
				KernelMode,
				FALSE,
				NULL);
			Status = DebugEvent->Status;
			*ApiMsg = DebugEvent->ApiMsg;
		}
	}
	else {
		if (!NT_SUCCESS(Status)) {
			ObfDereferenceObject(Process);
			ObfDereferenceObject(Thread);
			ExFreePool(DebugEvent);
		}
	}
	DbgPrintEx(77, 0,
		"[DbgHook] DbgkpQueueMessage return status=0x%08X pid=%lu api=%lu flags=0x%08X\n",
		Status,
		(ULONG)PsGetProcessId(Process),
		(ULONG)ApiMsg->ApiNumber,
		Flags);
	return Status;
}


BOOLEAN  DbgkForwardException(
	PEXCEPTION_RECORD ExceptionRecord,
	BOOLEAN DebugException,
	BOOLEAN SecondChance)
{
	DbgPrintEx(77, 0, "[DbgHook] DbgkForwardException enter pid=%lu DebugException=%d ExceptionCode=0x%08X\n", (ULONG)PsGetCurrentProcessId(), DebugException, ExceptionRecord->ExceptionCode);
	NTSTATUS		st;
	PEPROCESS		Process;
	PVOID			ExceptionPort;
	PDEBUG_OBJECT	DebugObject;
	BOOLEAN			bLpcPort;

	DBGKM_APIMSG m;
	PDBGKM_EXCEPTION args;

	RtlZeroMemory(&m, sizeof(m));

	DebugObject = NULL;
	ExceptionPort = NULL;
	bLpcPort = FALSE;

	args = &m.u.Exception;
	m.h.u1.Length = 0xD000A8;
	m.h.u2.ZeroInit = 8;
	m.ApiNumber = DbgKmExceptionApi;

	Process = (PEPROCESS)PsGetCurrentProcess();

	if (DebugException == TRUE)
	{
		KIRQL OldIrql = { 0 };
		KeAcquireSpinLock(&g_DebugLock, &OldIrql);
		for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
		{
			PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
			if (pDebuginfo->TargetProcessId == PsGetCurrentProcessId())
			{
				DebugObject = pDebuginfo->DebugObject;
				break;
			}
		}
		KeReleaseSpinLock(&g_DebugLock, OldIrql);
	}
	else
	{
		ExceptionPort = PsCaptureExceptionPort(Process);
		m.h.u2.ZeroInit = 0x7;
		bLpcPort = TRUE;
	}

	DbgPrintEx(77, 0, "[DbgHook] DbgkForwardException DebugObject=%p ExceptionPort=%p\n", DebugObject, ExceptionPort);

	if ((ExceptionPort == NULL && DebugObject == NULL) &&
		DebugException == TRUE)
	{
		return FALSE;
	}

	args->ExceptionRecord = *ExceptionRecord;
	args->FirstChance = !SecondChance;

	if (bLpcPort == FALSE)
	{
#ifdef WIN7
		st = DbgkpSendApiMessage(DebugException, &m);
#else
		st = DbgkpSendApiMessage(PsGetThreadProcess(KeGetCurrentThread()), DebugException, &m);
#endif

	}
	else if (ExceptionPort) {

		st = DbgkpSendApiMessageLpc(&m, ExceptionPort, DebugException);
		ObfDereferenceObject(ExceptionPort);
	}
	else {
		m.ReturnedStatus = DBG_EXCEPTION_NOT_HANDLED;
		st = STATUS_SUCCESS;
	}

	DbgPrintEx(77, 0, "[DbgHook] DbgkForwardException st=0x%08X ReturnedStatus=0x%08X\n", st, m.ReturnedStatus);

	if (NT_SUCCESS(st))
	{

		st = m.ReturnedStatus;

		if (m.ReturnedStatus == DBG_EXCEPTION_NOT_HANDLED)
		{
			if (DebugException == TRUE)
			{
				DbgPrintEx(77, 0, "[DbgHook] DbgkForwardException return FALSE (not handled)\n");
				return FALSE;
			}

			st = DbgkpSendErrorMessage(ExceptionRecord, 0, &m);
		}
	}

	DbgPrintEx(77, 0, "[DbgHook] DbgkForwardException return %d\n", NT_SUCCESS(st));
	return NT_SUCCESS(st);
}


VOID DbgkMapViewOfSection(
	PEPROCESS	Process,
	PVOID SectionObject,
	PVOID BaseAddress
)
{
	PTEB	Teb;
	HANDLE	hFile;
	DBGKM_APIMSG ApiMsg;
	PEPROCESS	CurrentProcess;
	PETHREAD	CurrentThread;
	PIMAGE_NT_HEADERS	pImageHeader;

	hFile = NULL;
	CurrentProcess = (PEPROCESS)PsGetCurrentProcess();
	CurrentThread = (PETHREAD)PsGetCurrentThread();

	if (ExGetPreviousMode() == KernelMode)
		return;


	PDEBUG_OBJECT	DebugObject = NULL;
	KIRQL OldIrql = { 0 };
	KeAcquireSpinLock(&g_DebugLock, &OldIrql);
	for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
	{
		PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
		if (pDebuginfo->TargetProcessId == PsGetCurrentProcessId())
		{
			DebugObject = pDebuginfo->DebugObject;
			break;
		}
	}
	KeReleaseSpinLock(&g_DebugLock, OldIrql);

	if (!DebugObject)
		return;

	Teb = (PTEB)PsGetThreadTeb(CurrentThread);

	if (Teb != NULL && Process == CurrentProcess)
	{
		if (!DbgkpSuppressDbgMsg(Teb))
		{
			ApiMsg.u.LoadDll.NamePointer = Teb->NtTib.ArbitraryUserPointer;
		}
		else {
			return;
		}
	}
	else {
		ApiMsg.u.LoadDll.NamePointer = NULL;
	}

	hFile = DbgkpSectionToFileHandle(SectionObject);
	ApiMsg.u.LoadDll.FileHandle = hFile;
	ApiMsg.u.LoadDll.BaseOfDll = BaseAddress;
	ApiMsg.u.LoadDll.DebugInfoFileOffset = 0;
	ApiMsg.u.LoadDll.DebugInfoSize = 0;

	_try{
		pImageHeader = RtlImageNtHeader(BaseAddress);
		if (pImageHeader != NULL)
		{
			ApiMsg.u.LoadDll.DebugInfoFileOffset = pImageHeader->FileHeader.PointerToSymbolTable;
			ApiMsg.u.LoadDll.DebugInfoSize = pImageHeader->FileHeader.NumberOfSymbols;
		}
	}_except(EXCEPTION_EXECUTE_HANDLER) {
		ApiMsg.u.LoadDll.DebugInfoFileOffset = 0;
		ApiMsg.u.LoadDll.DebugInfoSize = 0;
		ApiMsg.u.LoadDll.NamePointer = NULL;
	}
	ApiMsg.h.u1.Length = 0x500028;
	ApiMsg.h.u2.ZeroInit = 8;
	ApiMsg.ApiNumber = DbgKmLoadDllApi;

#ifdef WIN7
	DbgkpSendApiMessage(0x1, &ApiMsg);
#else
	DbgkpSendApiMessage(PsGetThreadProcess(KeGetCurrentThread()), 0x1, &ApiMsg);
#endif

	if (ApiMsg.u.LoadDll.FileHandle != NULL)
	{
		ObCloseHandle(ApiMsg.u.LoadDll.FileHandle, KernelMode);
	}
}


VOID DbgkUnMapViewOfSection(
	PEPROCESS	Process,
	PVOID	BaseAddress)
{
	PTEB	Teb;
	DBGKM_APIMSG ApiMsg;
	PEPROCESS	CurrentProcess;
	PETHREAD	CurrentThread;

	CurrentProcess = (PEPROCESS)PsGetCurrentProcess();
	CurrentThread = (PETHREAD)PsGetCurrentThread();

	if (ExGetPreviousMode() == KernelMode)
		return;

	PDEBUG_OBJECT	DebugObject = NULL;
	KIRQL OldIrql = { 0 };
	KeAcquireSpinLock(&g_DebugLock, &OldIrql);
	for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
	{
		PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
		if (pDebuginfo->TargetProcessId == PsGetCurrentProcessId())
		{
			DebugObject = pDebuginfo->DebugObject;
			break;
		}
	}
	KeReleaseSpinLock(&g_DebugLock, OldIrql);

	if (!DebugObject)
		return;

	//����ʡ����ϵͳ���̺͹ҿ����̵��ж�
	Teb = (PTEB)PsGetThreadTeb(CurrentThread);

	if (Teb != NULL && Process == CurrentProcess)
	{
		if (DbgkpSuppressDbgMsg(Teb))
		{
			return;
		}
	}
	ApiMsg.u.UnloadDll.BaseAddress = BaseAddress;
	ApiMsg.h.u1.Length = 0x380010;
	ApiMsg.h.u2.ZeroInit = 8;
	ApiMsg.ApiNumber = DbgKmUnloadDllApi;

#ifdef WIN7
	DbgkpSendApiMessage(0x1, &ApiMsg);
#else
	DbgkpSendApiMessage(PsGetThreadProcess(KeGetCurrentThread()), 0x1, &ApiMsg);
#endif
}


NTSTATUS  NtDebugActiveProcess(
	HANDLE ProcessHandle,
	HANDLE DebugObjectHandle)
{
	DbgPrintEx(77, 0, "[DbgHook] NtDebugActiveProcess enter pid=%lu\n", (ULONG)PsGetCurrentProcessId());
	NTSTATUS status;
	KPROCESSOR_MODE PreviousMode;
	PDEBUG_OBJECT DebugObject;
	BOOLEAN DebugObjectReferenced = FALSE;
	PEPROCESS Process, CurrentProcess;
	PETHREAD LastThread = NULL;
	PreviousMode = ExGetPreviousMode();
	status = ObReferenceObjectByHandle(
		ProcessHandle,
		0x800,
		*PsProcessType,
		PreviousMode,
		(PVOID*)& Process,
		NULL);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(77, 0, "[DbgHook] NtDebugActiveProcess ObRefProcess failed 0x%08X\n", status);
		return status;
	}

	DbgPrintEx(77, 0, "[DbgHook] NtDebugActiveProcess target pid=%lu\n", (ULONG)PsGetProcessId(Process));

	if (Process == (PEPROCESS)PsGetCurrentProcess() || Process == (PEPROCESS)PsInitialSystemProcess) {
		ObfDereferenceObject(Process);
		return STATUS_ACCESS_DENIED;
	}

	CurrentProcess = (PEPROCESS)PsGetCurrentProcess();
	status = ObReferenceObjectByHandle(
		DebugObjectHandle,
		0x2,
		*g_DbgkDebugObjectType,
		PreviousMode,
		(PVOID*)& DebugObject,
		NULL);
	DbgPrintEx(77, 0, "[DbgHook] NtDebugActiveProcess ObRefDebugObject status=0x%08X\n", status);
	if (NT_SUCCESS(status))
	{
		DebugObjectReferenced = TRUE;
	}
	KIRQL OldIrql = { 0 };
	KeAcquireSpinLock(&g_DebugLock, &OldIrql);
	for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
	{
		PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
		if (pDebuginfo->SourceProcessId == PsGetCurrentProcessId())
		{
			pDebuginfo->TargetProcessId = PsGetProcessId(Process);
			break;
		}
	}
	KeReleaseSpinLock(&g_DebugLock, OldIrql);

	if (NT_SUCCESS(status)) {

		DbgPrintEx(77, 0, "[DbgHook] Acquire   RundownProtection ... \n");
		PEX_RUNDOWN_REF RundownProtect = (PEX_RUNDOWN_REF)GetProcess_RundownProtect(Process);
		
		if (ExAcquireRundownProtection(RundownProtect))
		{
			DbgPrintEx(77, 0, "[DbgHook] Acquire   RundownProtection success\n");
			DbgPrintEx(77, 0,
				"[DbgHook] before DbgkpPostFakeProcessCreateMessages Process=%p DebugObject=%p LastThread=%p\n",
				Process, DebugObject, LastThread);
			DbgPrintEx(77, 0,
				"[DbgHook] posting initial process event Process=%p DebugObject=%p\n",
				Process, DebugObject);
			status = DbgkpPostFakeProcessCreateMessages(Process, DebugObject, &LastThread);
			DbgPrintEx(77, 0,
				"[DbgHook] DbgkpPostFakeProcessCreateMessages status=0x%08X LastThread=%p\n",
				status, LastThread);
			if (NT_SUCCESS(status))
			{
				status = DbgkpSetProcessDebugObject(
					(PEPROCESS)Process,
					DebugObject,
					STATUS_SUCCESS,
					LastThread);
				DbgPrintEx(77, 0,
					"[DbgHook] DbgkpSetProcessDebugObject status=0x%08X\n",
					status);
			}
			ExReleaseRundownProtection(RundownProtect);
		}
		else {
			status = STATUS_PROCESS_IS_TERMINATING;
		}
	}

	ObfDereferenceObject(Process);
	if (DebugObjectReferenced)
	{
		ObfDereferenceObject(DebugObject);
	}
	DbgPrintEx(77, 0, "[DbgHook] NtDebugActiveProcess return with status%d\n", status);
	return status;
}


VOID KiDispatchException(
	PEXCEPTION_RECORD ExceptionRecord,
	void* ExceptionFrame,
	PKTRAP_FRAME TrapFrame,
	KPROCESSOR_MODE PreviousMode,
	BOOLEAN FirstChance)
{
	if (PreviousMode != KernelMode)
	{
		BOOLEAN isDebug = FALSE;
		KIRQL OldIrql = { 0 };
		KeAcquireSpinLock(&g_DebugLock, &OldIrql);
		for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
		{
			PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
			if (pDebuginfo->TargetProcessId == PsGetCurrentProcessId())
			{
				isDebug = TRUE;
				break;
			}
		}
		KeReleaseSpinLock(&g_DebugLock, OldIrql);

		if (isDebug)
		{
			BOOLEAN Wow64ExceptionCode = FALSE;
			if ((TrapFrame->SegCs & 0xfff8) == KGDT64_R3_CMCODE)
			{
				switch (ExceptionRecord->ExceptionCode)
				{
				case STATUS_BREAKPOINT:
					ExceptionRecord->ExceptionCode = STATUS_WX86_BREAKPOINT;
					Wow64ExceptionCode = TRUE;
					break;
				case STATUS_SINGLE_STEP:
					ExceptionRecord->ExceptionCode = STATUS_WX86_SINGLE_STEP;
					Wow64ExceptionCode = TRUE;
					break;
				}
			}

			if (DbgkForwardException(ExceptionRecord, TRUE, FALSE))
			{
				return;
			}

			if (Wow64ExceptionCode)
			{
				switch (ExceptionRecord->ExceptionCode)
				{
				case STATUS_WX86_BREAKPOINT:
					ExceptionRecord->ExceptionCode = STATUS_BREAKPOINT;
					break;
				case STATUS_WX86_SINGLE_STEP:
					ExceptionRecord->ExceptionCode = STATUS_SINGLE_STEP;
					break;
				}
			}
		}
	}

	OrignalKiDispatchException(ExceptionRecord, ExceptionFrame, TrapFrame, PreviousMode, FirstChance);
	return;
}


NTSTATUS NtCreateUserProcess(
	PHANDLE ProcessHandle,
	PETHREAD ThreadHandle,
	ACCESS_MASK ProcessDesiredAccess,
	ACCESS_MASK ThreadDesiredAccess,
	PVOID ProcessObjectAttributes,
	PVOID ThreadObjectAttributes,
	ULONG ProcessFlags,
	ULONG ThreadFlags,
	PVOID ProcessParameters,
	void* CreateInfo,
	void* AttributeList)
{
	NTSTATUS status = 0;
	status = OrignalNtCreateUserProcess(ProcessHandle,
		ThreadHandle,
		ProcessDesiredAccess,
		ThreadDesiredAccess,
		ProcessObjectAttributes,
		ThreadObjectAttributes,
		ProcessFlags,
		ThreadFlags,
		ProcessParameters,
		CreateInfo,
		AttributeList);

	if (NT_SUCCESS(status) && ProcessHandle != NULL)
	{
		PDebugInfomation TmpDebuginfo = NULL;
		BOOLEAN isDebug = FALSE;
		KIRQL OldIrql = { 0 };
		KeAcquireSpinLock(&g_DebugLock, &OldIrql);
		for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
		{
			PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
			if (pDebuginfo->SourceProcessId == PsGetCurrentProcessId())
			{
				TmpDebuginfo = pDebuginfo;
				isDebug = TRUE;
				break;
			}
		}
		KeReleaseSpinLock(&g_DebugLock, OldIrql);

		if (isDebug)
		{
			PEPROCESS temp_process = NULL;
			status = ObReferenceObjectByHandle(*ProcessHandle, 0x0400, *PsProcessType, ExGetPreviousMode(), (void**)&temp_process, NULL);
			if (!NT_SUCCESS(status))
				return status;

			PVOID DebugPort__ = GetProcess_DebugPort(temp_process);
			if (*(ULONG64*)(DebugPort__) != 0)
			{
				HANDLE target_pid = PsGetProcessId(temp_process);
				TmpDebuginfo->TargetProcessId = target_pid;

				*(ULONG64*)(DebugPort__) = 0;
				DbgkpMarkProcessPeb(temp_process);

				PVOID Flags = GetProcess_ProcessFlags(temp_process);
				*(PULONG64)Flags &= ~PS_PROCESS_FLAGS_NO_DEBUG_INHERIT;
			}

		}
	}
	return status;
}


NTSTATUS NtTerminateProcess(
	HANDLE ProcessHandle,
	NTSTATUS ExitStatus)
{
	NTSTATUS st;
	PEPROCESS Process = NULL;
	if (ProcessHandle)
	{
		st = ObReferenceObjectByHandle(ProcessHandle,
			PROCESS_TERMINATE,
			*PsProcessType,
			ExGetPreviousMode(),
			(PVOID*)& Process,
			NULL);
	}
	else
	{
		Process = PsGetCurrentProcess();
	}

	if (Process)
	{
		KIRQL OldIrql = { 0 };
		KeAcquireSpinLock(&g_DebugLock, &OldIrql);
		for (PLIST_ENTRY pListEntry = g_Debuginfo.List.Flink; pListEntry != &g_Debuginfo.List; pListEntry = pListEntry->Flink)
		{
			PDebugInfomation pDebuginfo = CONTAINING_RECORD(pListEntry, DebugInfomation, List);
			if (pDebuginfo->TargetProcessId == PsGetProcessId(Process))
			{
				RemoveEntryList(&pDebuginfo->List);
				ExFreePool(pDebuginfo);
				break;
			}
		}
		KeReleaseSpinLock(&g_DebugLock, OldIrql);

		if (ProcessHandle)
			ObDereferenceObject(Process);
	}

	return OrignalNtTerminateProcess(ProcessHandle, ExitStatus);
}
