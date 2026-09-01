#include "SVM.h"
#include "VMCB.h"
#include <intrin.h>

//AMD SVM 没有专门的VMMCALL指令，只能使用自定义CPUID
//AMD 手册上给虚拟化预留的CPUID ID 为 0x40000000~0x400000ff
constexpr UINT32 GUEST_CALL_VMM_CPUID_FUNCTION = 0x400000ff;
constexpr UINT32 EXIT_SVM_CPUID_SUBFUNCTION = 0x00000000;
constexpr UINT32 IS_IN_SVM_CPUID_SUBFUNCTION = 0x00000001;
constexpr UINT32 SVM_TAG = MAKE_TAG('s', 'v', 'm', ' ');

//GDT表项，参考https://wiki.osdev.org/Global_Descriptor_Table#System_Segment_Descriptor
//照抄https://github.com/tandasat/SimpleSvm
typedef struct _SEGMENT_DESCRIPTOR
{
	union
	{
		UINT64 AsUInt64;
		struct
		{
			UINT16 LimitLow;        // [0:15] 
			UINT16 BaseLow;         // [16:31]
			UINT32 BaseMiddle : 8;  // [32:39]
			UINT32 Type : 4;        // [40:43]
			UINT32 System : 1;      // [44]
			UINT32 Dpl : 2;         // [45:46]
			UINT32 Present : 1;     // [47]
			UINT32 LimitHigh : 4;   // [48:51]
			UINT32 Avl : 1;         // [52]
			UINT32 LongMode : 1;    // [53]
			UINT32 DefaultBit : 1;  // [54]
			UINT32 Granularity : 1; // [55]
			UINT32 BaseHigh : 8;    // [56:63]	
		} Fields;
	};
	//这一部分是我自己添加，其余部分和git项目一样
	//这一部分是否存在看Type成员，X64强制平坦段，对于代码段和数据段这一部分不存在
	//对于门段(gate segment)和系统段(system segment)才有这些成员，而且定义各有不同，具体参考CPU手册
	//这里我需要获取系统段(TSS LDT)的基址
	struct
	{
		UINT32 BaseHigh4Byte;
		UINT32 Reserved;
	} OptionalField;
} SEGMENT_DESCRIPTOR, * PSEGMENT_DESCRIPTOR;

//这个结构见这个网址 https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170
//里面对 MASM .PUSHFRAME 指令的行为分析
typedef struct _MACHINE_FRAME
{
	UINT64 Rip;
	UINT64 Cs;
	UINT64 EFlags;
	UINT64 OldRsp;
	UINT64 Ss;
} MACHINE_FRAME, * PMACHINE_FRAME;

//这个函数完全照抄https://github.com/tandasat/SimpleSvm
//原函数名字是SvGetSegmentAccessRight
//获取段寄存器Attribute
#pragma code_seg("PAGE")
SegmentAttribute GetSegmentAttribute(_In_ UINT16 SegmentSelector, _In_ ULONG_PTR GdtBase)
{
	PAGED_CODE();
	PSEGMENT_DESCRIPTOR descriptor = NULL;
	SEGMENT_ATTRIBUTE attribute = {};

	//关于段选择子的结构参考https://wiki.osdev.org/Segment_Selector
	//低3bit是标志，这里不管基址是LDT的情况，这个函数设计就是假设基址是GDT
	descriptor = reinterpret_cast<PSEGMENT_DESCRIPTOR>(
		GdtBase + (SegmentSelector & ~0x7));

	attribute.Fields.Type = descriptor->Fields.Type;
	attribute.Fields.System = descriptor->Fields.System;
	attribute.Fields.Dpl = descriptor->Fields.Dpl;
	attribute.Fields.Present = descriptor->Fields.Present;
	attribute.Fields.Avl = descriptor->Fields.Avl;
	attribute.Fields.LongMode = descriptor->Fields.LongMode;
	attribute.Fields.DefaultBit = descriptor->Fields.DefaultBit;
	attribute.Fields.Granularity = descriptor->Fields.Granularity;
	attribute.Fields.Reserved1 = 0;

	return attribute;
}

//获取段寄存器Base
#pragma code_seg("PAGE")
UINT64 GetSegmentBaseAddress(_In_ UINT16 SegmentSelector, _In_ ULONG_PTR GdtBase)
{
	PAGED_CODE();
	PSEGMENT_DESCRIPTOR descriptor;
	UINT64 baseAddress = 0;

	descriptor = reinterpret_cast<PSEGMENT_DESCRIPTOR>(
		GdtBase + (SegmentSelector & ~0x7));

	baseAddress |= descriptor->Fields.BaseLow;
	baseAddress |= ((UINT64)descriptor->Fields.BaseMiddle) << 16;
	baseAddress |= ((UINT64)descriptor->Fields.BaseHigh) << 24;
	if (!descriptor->Fields.System)
		baseAddress |= ((UINT64)descriptor->OptionalField.BaseHigh4Byte) << 32;

	return baseAddress;
}

//获取段寄存器Limit
#pragma code_seg("PAGE")
UINT32 GetSegmentLimit2(_In_ UINT16 SegmentSelector, _In_ ULONG_PTR GdtBase)
{
	PAGED_CODE();
	PSEGMENT_DESCRIPTOR descriptor;
	UINT32 limit = 0;

	descriptor = reinterpret_cast<PSEGMENT_DESCRIPTOR>(
		GdtBase + (SegmentSelector & ~0x7));

	limit |= descriptor->Fields.LimitLow;
	limit |= ((UINT64)descriptor->Fields.LimitHigh) << 16;

	/*
		获取段描述符中的 limit 字段：段描述符中的 limit 字段是 20 位的值，分为高 4 位和低 16 位。
		检查粒度（G）位：
			如果 G = 0，limit 的单位是字节，范围是 1B 到 1MB。
			如果 G = 1，limit 的单位是 4KB，范围是 4KB 到 4GB。
		计算 limit：
			当 G = 0 时，limit 的计算公式为： {Limit} = {Low 16 bits} + ({High 4 bits} << 16)
			当 G = 1 时，limit 的计算公式为： {Limit} = (({Low 16 bits} + ({High 4 bits} << 16))) << 12) + 0xFFF
	*/

	if (descriptor->Fields.Granularity)
	{
		limit <<= 12;
		limit |= 0xfff;
	}

	return limit;
}

//检查寄存器
#pragma code_seg()
extern "C" void CompareGenericRegisters(const GenericRegisters& genericRegistersNow, GenericRegisters& genericRegistersPrev)
{

#define ASSERT_REGISTER_IS_EQUAL(regField) \
	NT_ASSERT(!RtlCompareMemory(&genericRegistersNow.regField, &genericRegistersPrev.regField, sizeof(genericRegistersNow.regField)))

	if (!KdDebuggerNotPresent)
	{
		ASSERT_REGISTER_IS_EQUAL(xmm0);
		ASSERT_REGISTER_IS_EQUAL(xmm1);
		ASSERT_REGISTER_IS_EQUAL(xmm2);
		ASSERT_REGISTER_IS_EQUAL(xmm3);
		ASSERT_REGISTER_IS_EQUAL(xmm4);
		ASSERT_REGISTER_IS_EQUAL(xmm5);
		ASSERT_REGISTER_IS_EQUAL(xmm6);
		ASSERT_REGISTER_IS_EQUAL(xmm7);
		ASSERT_REGISTER_IS_EQUAL(xmm8);
		ASSERT_REGISTER_IS_EQUAL(xmm9);
		ASSERT_REGISTER_IS_EQUAL(xmm10);
		ASSERT_REGISTER_IS_EQUAL(xmm11);
		ASSERT_REGISTER_IS_EQUAL(xmm12);
		ASSERT_REGISTER_IS_EQUAL(xmm13);
		ASSERT_REGISTER_IS_EQUAL(xmm14);
		ASSERT_REGISTER_IS_EQUAL(xmm15);
		ASSERT_REGISTER_IS_EQUAL(r15);
		ASSERT_REGISTER_IS_EQUAL(r14);
		ASSERT_REGISTER_IS_EQUAL(r13);
		ASSERT_REGISTER_IS_EQUAL(r12);
		ASSERT_REGISTER_IS_EQUAL(r11);
		ASSERT_REGISTER_IS_EQUAL(r10);
		ASSERT_REGISTER_IS_EQUAL(r9);
		ASSERT_REGISTER_IS_EQUAL(r8);
		ASSERT_REGISTER_IS_EQUAL(rbp);
		ASSERT_REGISTER_IS_EQUAL(rsi);
		ASSERT_REGISTER_IS_EQUAL(rdi);
		ASSERT_REGISTER_IS_EQUAL(rdx);
		ASSERT_REGISTER_IS_EQUAL(rcx);
		ASSERT_REGISTER_IS_EQUAL(rbx);
	}

#undef ASSERT_REGISTER_IS_EQUAL
}

//初始化KTRAP_FRAME结构体
#pragma code_seg()
extern "C" void FillMachineFrame(MACHINE_FRAME& machineFrame, const GenericRegisters& guestRegistars, const VirtCpuInfo& virtCpuInfo)
{
	UNREFERENCED_PARAMETER(guestRegistars);
	machineFrame = {};

	machineFrame.Rip = virtCpuInfo.guestVmcb.controlFields.nRip;
	machineFrame.Cs = virtCpuInfo.guestVmcb.statusFields.cs.selector;
	machineFrame.Ss = virtCpuInfo.guestVmcb.statusFields.ss.selector;
	machineFrame.OldRsp = virtCpuInfo.guestVmcb.statusFields.rsp;
	machineFrame.EFlags = (UINT32)virtCpuInfo.guestVmcb.statusFields.rflags;
}

//#VMEXIT处理函数
#pragma code_seg()
extern "C" void VmExitHandler(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters, PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr)
{
	pGuestRegisters->rip = pVirtCpuInfo->guestVmcb.statusFields.rip;
	pGuestRegisters->rsp = pVirtCpuInfo->guestVmcb.statusFields.rsp;
	pGuestRegisters->rax = pVirtCpuInfo->guestVmcb.statusFields.rax;
	pGuestRegisters->rflags = pVirtCpuInfo->guestVmcb.statusFields.rflags;

	IMsrBackupRestorePlugin* pMsrHookPlugin = pVirtCpuInfo->otherInfo.pSvmManager->pMsrBackupRestorePlugin;

	//如果 MSR 拦截插件存在，进入VM之后保存Guest和恢复Host的MSR
	if (pMsrHookPlugin != NULL)
	{
		pMsrHookPlugin->SaveGuestMsrForCpu(pVirtCpuInfo->otherInfo.cpuIdx);
		pMsrHookPlugin->LoadHostMsrForCpu(pVirtCpuInfo->otherInfo.cpuIdx);
	}

	//转发到SVMManager::VmExitHandler函数
	pVirtCpuInfo->otherInfo.pSvmManager->VmExitHandler(pVirtCpuInfo, pGuestRegisters, pGuestVmcbPhyAddr, pHostVmcbPhyAddr);

	//如果 MSR 拦截插件存在，退出VM之前恢复Guest和保存Host的MSR
	if (pMsrHookPlugin != NULL)
	{
		pMsrHookPlugin->SaveHostMsrForCpu(pVirtCpuInfo->otherInfo.cpuIdx);
		pMsrHookPlugin->LoadGuestMsrForCpu(pVirtCpuInfo->otherInfo.cpuIdx);
	}

	pVirtCpuInfo->guestVmcb.statusFields.rip = pGuestRegisters->rip;
	pVirtCpuInfo->guestVmcb.statusFields.rsp = pGuestRegisters->rsp;
	pVirtCpuInfo->guestVmcb.statusFields.rax = pGuestRegisters->rax;
	pVirtCpuInfo->guestVmcb.statusFields.rflags = pGuestRegisters->rflags;
}

//获取CPU生产商的字符串
#pragma code_seg("PAGE")
void CPUString(char* outputString)
{
	PAGED_CODE();
	UINT32 cpuid_result[4] = {};
	__cpuidex((int*)cpuid_result, 0, 0);
	memcpy(outputString, &cpuid_result[1], sizeof(UINT32));
	memcpy(outputString + sizeof(UINT32), &cpuid_result[3], sizeof(UINT32));
	memcpy(outputString + sizeof(UINT32) * 2, &cpuid_result[2], sizeof(UINT32));
	outputString[3 * sizeof(UINT32)] = 0;
}

//分配MSR拦截标志位map
#pragma code_seg("PAGE")
NTSTATUS MsrPremissionsMapManager::Init()
{
	PAGED_CODE();
	if (IsInited())
		return STATUS_SUCCESS;

	constexpr UINT32 BITS_PER_MSR = 2;
	//FIRST_MSR_RANGE_BASE = 0x00000000;
	//FIRST_MSRPM_OFFSET = 0x000 * CHAR_BIT;
	constexpr UINT32 SECOND_MSR_RANGE_BASE = 0xc0000000;
	constexpr UINT32 SECOND_MSRPM_OFFSET = 0x800 * CHAR_BIT;
	constexpr UINT32 THIRD_MSR_RANGE_BASE = 0xc0010000;
	constexpr UINT32 THIRD_MSRPM_OFFSET = 0x1000 * CHAR_BIT;
	constexpr ULONG EFER_OFFSET = SECOND_MSRPM_OFFSET + ((IA32_MSR_EFER - SECOND_MSR_RANGE_BASE) * BITS_PER_MSR);
	constexpr ULONG VM_CR_OFFSET = THIRD_MSRPM_OFFSET + ((IA32_MSR_VM_CR - THIRD_MSR_RANGE_BASE) * BITS_PER_MSR);
	RTL_BITMAP bitmapHeader = {};

	//分配物理连续内存
	pMsrPremissionsMapVirtAddr = AllocContiguousMem(2ULL * PAGE_SIZE, SVM_TAG);
	if (pMsrPremissionsMapVirtAddr == NULL)
	{
		KdPrint(("MsrPremissionsMapManager::Init(): Memory not enough!\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	//初始化内存的值
	RtlInitializeBitMap(&bitmapHeader, (PULONG)pMsrPremissionsMapVirtAddr, 2 * PAGE_SIZE * CHAR_BIT);
	RtlClearAllBits(&bitmapHeader);

	//EFER读取写入拦截
	RtlSetBits(&bitmapHeader, EFER_OFFSET, 2);
	//VM_CR读取写入拦截
	RtlSetBits(&bitmapHeader, VM_CR_OFFSET, 2);

	//如果MSR拦截插件存在，让插件设置拦截标志位
	if (pMsrInterceptPlugin != NULL)
		pMsrInterceptPlugin->SetMsrPremissionMap(bitmapHeader);

	//获取物理地址
	pMsrPremissionsMapPhyAddr = (PVOID)MmGetPhysicalAddress(pMsrPremissionsMapVirtAddr).QuadPart;
	return STATUS_SUCCESS;
}

#pragma code_seg("PAGE")
void MsrPremissionsMapManager::Deinit()
{
	PAGED_CODE();
	if (pMsrPremissionsMapVirtAddr != NULL)
	{
		FreeContigousMem(pMsrPremissionsMapVirtAddr, SVM_TAG);
		pMsrPremissionsMapVirtAddr = NULL;
		pMsrPremissionsMapPhyAddr = NULL;
	}
}

#pragma code_seg("PAGE")
SVMStatus SVMManager::CheckSVM()
{
	PAGED_CODE();
	char szCpuString[13];
	CPUString(szCpuString);
	//检查是否是AMD CPU
	if (strcmp(szCpuString, "AuthenticAMD"))
		return SVMS_NONAMDCPU;

	SVMStatus result = SVMS_UNUSED;
	UINT32 cpuid_result[4] = {};

	do
	{
		//查询SMV支持
		__cpuidex((int*)cpuid_result, CPUID_FN_SVM_FEATURE, 0);

		//CPUID Fn 8000_0001h ecx 第 2 位 (0 base 下同) 是否为 1
		if (!(cpuid_result[2] & (1UL << CPUID_FN_80000001_ECX_SVM_OFFSET)))
			break;

		result = ((SVMStatus)(result | SVMS_SUPPORTED));

		//查询SVM启用
		UINT64 msrValue = __readmsr(IA32_MSR_VM_CR);

		//VM_CR MSR 寄存器 第 4 位 SVMDIS 是否为 0
		if (msrValue & 1ULL << VM_CR_SVMDIS_OFFSET)
			break;

		result = ((SVMStatus)(result | SVMS_ENABLED));

		//查询NPT启用

		__cpuidex((int*)cpuid_result, CPUID_FN_NPT_FEATURE, 0);

		//CPUID Fn 8000_000Ah edx 第 0 位 是否为 1
		if (!(cpuid_result[3] & (1UL << NPT_ENABLE_OFFSET)))
			break;

		result = ((SVMStatus)(result | SVMS_NPT_ENABLED));

	} while (false);

	return result;
}

#pragma code_seg("PAGE")
NTSTATUS SVMManager::Init()
{
	PAGED_CODE();
	NTSTATUS status = STATUS_SUCCESS;
	UINT32 idx = 0;

	do
	{
		//检查是否支持AMD-V
		SVMStatus svmStatus = CheckSVM();

		status = STATUS_INSUFFICIENT_RESOURCES;

		//虚拟化硬件检查
		if (svmStatus & SVMStatus::SVMS_NONAMDCPU)
		{
			KdPrint(("SVMManager::Init(): Not AMD Processor!\n"));
			break;
		}

		if (!(svmStatus & SVMStatus::SVMS_SUPPORTED))
		{
			KdPrint(("SVMManager::Init(): SVM feature is not supported!\n"));
			break;
		}

		if (!(svmStatus & SVMStatus::SVMS_ENABLED))
		{
			KdPrint(("SVMManager::Init(): SVM feature is not enabled!\n"));
			break;
		}

		//如果提供了NPT页表而且SVM不支持NPT，那么提示不支持NPT
		if (!(svmStatus & SVMStatus::SVMS_NPT_ENABLED) && pNCr3Provider != NULL)
		{
			KdPrint(("SVMManager::Init(): NPT feature is not enabled!\n"));
			break;
		}

		msrPremissionMap.SetPlugin(pMsrInterceptPlugin);

		//为每一个CPU分配进入虚拟化必备的资源
		//这里先初始化每个CPU的资源指针
		cpuCnt = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
		pVirtCpuInfo = (VirtCpuInfo**)AllocNonPagedMem(cpuCnt * sizeof(VirtCpuInfo*), SVM_TAG);

		if (pVirtCpuInfo == NULL)
		{
			KdPrint(("SVMManager::Init(): Cpu virtualization memory failed!\n"));
			break;
		}

		status = msrPremissionMap.Init();
		if (!NT_SUCCESS(status))
		{
			KdPrint(("SVMManager::Init(): MSR premission map init failed!\n"));
			break;
		}

		status = STATUS_SUCCESS;

		//为每个CPU分配进入虚拟化所需的内存
		for (idx = 0; idx < cpuCnt; ++idx)
		{
			pVirtCpuInfo[idx] = (VirtCpuInfo*)AllocNonPagedMem(sizeof(VirtCpuInfo), SVM_TAG);
			if (pVirtCpuInfo[idx] == NULL)
			{
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}
			RtlZeroMemory(pVirtCpuInfo[idx], sizeof(VirtCpuInfo));
			pVirtCpuInfo[idx]->otherInfo.pSvmManager = this;
			pVirtCpuInfo[idx]->otherInfo.cpuIdx = idx;
		}

		if (!NT_SUCCESS(status))
		{
			KdPrint(("SVMManager::Init(): CPU Virtualization resource init failed!\n"));
			break;
		}

		//进入虚拟化
		status = EnterVirtualization();

		if (!NT_SUCCESS(status))
		{
			KdPrint(("SVMManager::Init(): Can not enter virtualization!\n"));
			break;
		}

	} while (false);

	if (!NT_SUCCESS(status))
		Deinit();

	return status;
}

#pragma code_seg("PAGE")
void SVMManager::Deinit()
{
	PAGED_CODE();
	if (pVirtCpuInfo != NULL && cpuCnt)
	{
		LeaveVirtualization();

		for (SIZE_TYPE idx = 0; idx < cpuCnt; ++idx)
		{
			FreeNonPagedMem(pVirtCpuInfo[idx], SVM_TAG);
			pVirtCpuInfo[idx] = NULL;
		}
		FreeNonPagedMem(pVirtCpuInfo, SVM_TAG);
		pVirtCpuInfo = NULL;
		cpuCnt = 0;
	}
	msrPremissionMap.Deinit();
}

#pragma code_seg("PAGE")
NTSTATUS SVMManager::EnterVirtualization()
{
	PAGED_CODE();

	auto enterVirtualizationCore = [this](UINT32 cpuIdx) -> NTSTATUS
		{
			GenericRegisters registerBackup = {};

			_save_or_load_regs(&registerBackup);

			if (!pVirtCpuInfo[cpuIdx]->otherInfo.isInVirtualizaion)
			{
				//标记已经进入过虚拟化
				pVirtCpuInfo[cpuIdx]->otherInfo.isInVirtualizaion = TRUE;

				UINT64 eferBackup = __readmsr(IA32_MSR_EFER);
				__writemsr(IA32_MSR_SVM_MSR_VM_HSAVE_PA, MmGetPhysicalAddress(&pVirtCpuInfo[cpuIdx]->hostStatus).QuadPart);
				__writemsr(IA32_MSR_EFER, eferBackup | (1ULL << EFER_SVME_OFFSET));

				pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.interceptOpcodes1
					= Opcode1InterceptBits::CPUID | Opcode1InterceptBits::RDMSR_WRMSR;
				//vmrun拦截必须打开，否则vmrun会失败
				pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.interceptOpcodes2
					= Opcode2InterceptBits::VMRUN;

				//如果断点拦截插件存在，打开断点拦截
				if (pBreakpointInterceptPlugin != NULL)
					pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.interceptExceptionX = (1UL << BP_EXCEPTION_VECTOR_INDEX);

				//如果UD拦截插件存在，打开UD拦截
				if (pInvalidOpcodeInterceptPlugin != NULL)
					pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.interceptExceptionX |= (1UL << UD_EXCEPTION_VECTOR_INDEX);

				//如果DE拦截插件存在，打开DE拦截
				if (pSingleStepInterceptPlugin != NULL)
					pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.interceptExceptionX |= (1LL << DB_EXCEPTION_VECTOR_INDEX);

				pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.msrpmBasePA = msrPremissionMap.GetPhyAddress();
				pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.guestASID = 1;

				if (pNCr3Provider != NULL)
				{
					PVOID nCr3Pa = pNCr3Provider->GetNCr3ForCore(cpuIdx);
					if (nCr3Pa != (PVOID)INVALID_ADDR)
					{
						KdPrint(("SVMManager::EnterVirtualization(): Enable NPT\n"));
						pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.extendFeatures1.fields.enableNestedPage = true;
						pVirtCpuInfo[cpuIdx]->guestVmcb.controlFields.nCr3 = (UINT64)nCr3Pa;
					}
				}

				SAVE_GUEST_STATUS_FROM_REGS(pVirtCpuInfo[cpuIdx], &registerBackup, registerBackup.rflags, registerBackup.rsp, registerBackup.rip);

				pVirtCpuInfo[cpuIdx]->hostVmcb.statusFields = pVirtCpuInfo[cpuIdx]->guestVmcb.statusFields;

				//需要时禁用syscall和sysret
				if (!enableSce)
					pVirtCpuInfo[cpuIdx]->guestVmcb.statusFields.efer &= ~(1 << SCE_ENABLE_OFFSET);
				else
					pVirtCpuInfo[cpuIdx]->guestVmcb.statusFields.efer |= (UINT64)(1 << SCE_ENABLE_OFFSET);

				//__svm_vmsave((size_t)MmGetPhysicalAddress(&pVirtCpuInfo[cpuIdx]->guestVmcb).QuadPart);
				//__svm_vmsave((size_t)MmGetPhysicalAddress(&pVirtCpuInfo[cpuIdx]->hostVmcb).QuadPart);

				//如何MSR HOOK插件存在，进入VM之前保存Guest和Host的MSR
				if (pMsrBackupRestorePlugin != NULL)
				{
					pMsrBackupRestorePlugin->SaveGuestMsrForCpu(cpuIdx);
					pMsrBackupRestorePlugin->SaveHostMsrForCpu(cpuIdx);
				}

				_run_svm_vmrun
				(
					pVirtCpuInfo[cpuIdx],
					(PVOID)MmGetPhysicalAddress(&pVirtCpuInfo[cpuIdx]->guestVmcb).QuadPart,
					(PVOID)MmGetPhysicalAddress(&pVirtCpuInfo[cpuIdx]->hostVmcb).QuadPart,
					pVirtCpuInfo[cpuIdx]->stack1 + sizeof pVirtCpuInfo[cpuIdx]->stack1
				);

				//不应该返回
				//如果返回代表vmrun失败
				//直接 BugCheck
				__debugbreak();
				KeBugCheck(MANUALLY_INITIATED_CRASH);
			}

			return STATUS_SUCCESS;
		};

	return RunOnEachCore(0, cpuCnt, enterVirtualizationCore);
}

#pragma code_seg("PAGE")
void SVMManager::LeaveVirtualization()
{
	PAGED_CODE();

	//调用CPUID指令通知VMM退出
	auto coreAction = [this](UINT32 idx) -> NTSTATUS
	{
		int result[4] = {};
		if (pVirtCpuInfo[idx] != NULL)
		{
			//如果已经进入虚拟化，则按照核心退出虚拟化
			if (pVirtCpuInfo[idx]->otherInfo.isInVirtualizaion)
			{
				__cpuidex(result, GUEST_CALL_VMM_CPUID_FUNCTION, 0);
				pVirtCpuInfo[idx]->otherInfo.isInVirtualizaion = FALSE;
			}
		}
		return STATUS_SUCCESS;
	};

	RunOnEachCore(0, cpuCnt, coreAction);
}

#pragma code_seg()
void SVMManager::VmExitHandler(VirtCpuInfo* pVMMVirtCpuInfo, GenericRegisters* pGuestRegisters, PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr)
{
	UNREFERENCED_PARAMETER(pHostVmcbPhyAddr);
	UNREFERENCED_PARAMETER(pGuestVmcbPhyAddr);

	//先清空寄存器，代表这次执行后不退出虚拟机
	pGuestRegisters->extraInfo1 = 0;
	pGuestRegisters->extraInfo2 = 0;

	switch ((VmExitReasons)pVMMVirtCpuInfo->guestVmcb.controlFields.exitCode)
	{
	case VMEXIT_REASON_CPUID:
	{
		if (pCpuIdInterceptPlugin != NULL &&
			pCpuIdInterceptPlugin->HandleCpuid(pVMMVirtCpuInfo, pGuestRegisters,
				pGuestVmcbPhyAddr, pHostVmcbPhyAddr))
			break;

		if (((int)pGuestRegisters->rax) == GUEST_CALL_VMM_CPUID_FUNCTION)
		{
			switch (pGuestRegisters->rcx)
			{
			case EXIT_SVM_CPUID_SUBFUNCTION:
			{
				//如果不是从内核模式调用退出则忽略
				if (!IsKernelAddress((PVOID)pVMMVirtCpuInfo->guestVmcb.controlFields.nRip))
					break;

				//通过
				//设置pGuestRegisters->extraInfo1为&pVMMVirtCpuInfo->regsBackup.genericRegisters1 和 
				//设置pGuestRegisters->extraInfo2为pVMMVirtCpuInfo->guestVmcb.controlFields.nRip
				//告知_run_svm_vmrun退出vmm
				pGuestRegisters->extraInfo1 = (UINT64)&pVMMVirtCpuInfo->regsBackup.genericRegisters1;
				pGuestRegisters->extraInfo2 = (UINT64)pVMMVirtCpuInfo->guestVmcb.controlFields.nRip;

				//在gif打开但是没有退出VMM这一段时间禁用中断
				_disable();
				//在退出VMM时打开GIF，否则退出后系统会接收不了中断假死，在进入Host模式的时候GIF是关闭状态
				__svm_stgi();
				//加载guest状态
				__svm_vmload((SIZE_TYPE)pGuestVmcbPhyAddr);

				//退出虚拟化并继续执行guest
				UINT64 eferVal = __readmsr(IA32_MSR_EFER);
				__writemsr(IA32_MSR_EFER, eferVal & ~(1ULL << EFER_SVME_OFFSET));

				break;
			}
			case IS_IN_SVM_CPUID_SUBFUNCTION:
			{
				*reinterpret_cast<UINT32*>(&pGuestRegisters->rax) = 'IN';
				*reinterpret_cast<UINT32*>(&pGuestRegisters->rbx) = 'AMD';
				*reinterpret_cast<UINT32*>(&pGuestRegisters->rcx) = 'SVM';
				break;
			}
			}
		}
		else
		{
			int cpuidResult[4] = {};

			__cpuidex(cpuidResult, (int)pGuestRegisters->rax, (int)pGuestRegisters->rcx);

			if (((int)pGuestRegisters->rax) == CPUID_FN_SVM_FEATURE)
				cpuidResult[2] &= ~(1UL << CPUID_FN_80000001_ECX_SVM_OFFSET);

			*reinterpret_cast<UINT32*>(&pGuestRegisters->rax) = cpuidResult[0];
			*reinterpret_cast<UINT32*>(&pGuestRegisters->rbx) = cpuidResult[1];
			*reinterpret_cast<UINT32*>(&pGuestRegisters->rcx) = cpuidResult[2];
			*reinterpret_cast<UINT32*>(&pGuestRegisters->rdx) = cpuidResult[3];
		}

		//guest到下一条指令执行
		pGuestRegisters->rip = pVMMVirtCpuInfo->guestVmcb.controlFields.nRip;

		break;
	}
	case VMEXIT_REASON_MSR:
	{
		ULARGE_INTEGER value = {};
		UINT32 msrNum = (UINT32)pGuestRegisters->rcx;
		bool isWriteAccess = pVMMVirtCpuInfo->guestVmcb.controlFields.exitInfo1;

		if (isWriteAccess)
		{
			value.LowPart = (UINT32)pGuestRegisters->rax;
			value.HighPart = (UINT32)pGuestRegisters->rdx;

			if (pMsrInterceptPlugin != NULL &&
				pMsrInterceptPlugin->HandleMsrInterceptWrite(pVMMVirtCpuInfo, pGuestRegisters,
					pGuestVmcbPhyAddr, pHostVmcbPhyAddr,
					msrNum))
				break;

			//不允许客户机设置 EFER MSR 的 SVME 位 和 VM_CR MSR 的 SVMDIS 位
			if (msrNum == IA32_MSR_EFER && !(value.LowPart & (1UL << EFER_SVME_OFFSET)) ||
				msrNum == IA32_MSR_VM_CR && !(value.LowPart & (1ULL << VM_CR_SVMDIS_OFFSET)))
			{
				__debugbreak();
				KeBugCheck(MANUALLY_INITIATED_CRASH);
			}

			__writemsr(msrNum, value.QuadPart);
		}
		else
		{
			if (pMsrInterceptPlugin != NULL &&
				pMsrInterceptPlugin->HandleMsrImterceptRead(pVMMVirtCpuInfo, pGuestRegisters,
					pGuestVmcbPhyAddr, pHostVmcbPhyAddr,
					msrNum))
				break;

			value.QuadPart = __readmsr(msrNum);

			if (msrNum == IA32_MSR_VM_CR)
				value.QuadPart |= (1ULL << VM_CR_SVMDIS_OFFSET);
			if (msrNum == IA32_MSR_EFER)
				value.QuadPart &= ~(1UL << EFER_SVME_OFFSET);

			*reinterpret_cast<UINT32*>(&pGuestRegisters->rax) = value.LowPart;
			*reinterpret_cast<UINT32*>(&pGuestRegisters->rdx) = value.HighPart;
		}

		//guest到下一条指令执行
		pGuestRegisters->rip = pVMMVirtCpuInfo->guestVmcb.controlFields.nRip;

		break;
	}
	case VMEXIT_REASON_EXCEPTION_BP:
	{
		if (pBreakpointInterceptPlugin != NULL &&
			pBreakpointInterceptPlugin->HandleBreakpoint(pVMMVirtCpuInfo, pGuestRegisters, pGuestVmcbPhyAddr, pHostVmcbPhyAddr))
			break;

		//默认直接注入断点异常由guest处理
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.data = 0;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vector = BP_EXCEPTION_VECTOR_INDEX;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.type = 3;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vaild = 1;
		pGuestRegisters->rip = pVMMVirtCpuInfo->guestVmcb.controlFields.nRip;
		break;
	}
	case VMEXIT_REASON_EXCEPTION_UD:
	{
		if (pInvalidOpcodeInterceptPlugin != NULL &&
			pInvalidOpcodeInterceptPlugin->HandleInvalidOpcode(pVMMVirtCpuInfo, pGuestRegisters, pGuestVmcbPhyAddr, pHostVmcbPhyAddr))
			break;

		//注入UD异常由guest处理
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.data = 0;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vector = UD_EXCEPTION_VECTOR_INDEX;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.type = 3;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vaild = 1;
		break;
	}
	case VMEXIT_REASON_EXCEPTION_DB:
	{
		if (pSingleStepInterceptPlugin != NULL &&
			pSingleStepInterceptPlugin->HandleSignleStep(pVMMVirtCpuInfo, pGuestRegisters, pGuestVmcbPhyAddr, pHostVmcbPhyAddr))
			break;

		//注入DB异常由guest处理
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.data = 0;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vector = DB_EXCEPTION_VECTOR_INDEX;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.type = 3;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vaild = 1;
		break;
	}
	case VMEXIT_REASON_VMRUN:
	{
		//注入断点异常由guest处理
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.data = 0;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vector = BP_EXCEPTION_VECTOR_INDEX;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.type = 3;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vaild = 1;
		pGuestRegisters->rip = pVMMVirtCpuInfo->guestVmcb.controlFields.nRip;
		break;
	}
	case VMEXIT_REASON_NPF:
	{
		if (pNpfInterceptPlugin != NULL &&
			pNpfInterceptPlugin->HandleNpf(pVMMVirtCpuInfo, pGuestRegisters, pGuestVmcbPhyAddr, pHostVmcbPhyAddr))
			break;

		__debugbreak();
		KeBugCheck(MANUALLY_INITIATED_CRASH);
		break;
	}
	default:
	{
		__debugbreak();
		KeBugCheck(MANUALLY_INITIATED_CRASH);
		break;
	}
	}

	//EFLAGS TF 标志位为 1 则附加DE中断

	//原因见如下网址
	//https://howtohypervise.blogspot.com/2019/01/a-common-missight-in-most-hypervisors.html

	//你也可以看泄露的VMP源码，里面就使用了这种方法检测虚拟化

	//硬件断电和其他复杂断点的处理我并没有写，需要的话自己加

	if (pGuestRegisters->rflags & (1ULL << EFLAGS_TF_OFFSET))
	{
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.data = 0;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vector = DB_EXCEPTION_VECTOR_INDEX;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.type = 3;
		pVMMVirtCpuInfo->guestVmcb.controlFields.eventInj.fields.vaild = 1;
	}
}
