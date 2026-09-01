#ifndef SVM_H
#define SVM_H

#include "Basic.h"
#include "VMCB.h"

// 传入 #VMEXIT 处理函数，用于处理修改guest寄存器状态
// 也用于 进入虚拟化前后的寄存器备份和恢复
struct GenericRegisters
{
	M128A xmm0;
	M128A xmm1;
	M128A xmm2;
	M128A xmm3;
	M128A xmm4;
	M128A xmm5;
	M128A xmm6;
	M128A xmm7;
	M128A xmm8;
	M128A xmm9;
	M128A xmm10;
	M128A xmm11;
	M128A xmm12;
	M128A xmm13;
	M128A xmm14;
	M128A xmm15;
	UINT64 r15;
	UINT64 r14;
	UINT64 r13;
	UINT64 r12;
	UINT64 r11;
	UINT64 r10;
	UINT64 r9;
	UINT64 r8;
	UINT64 rbp;
	UINT64 rsi;
	UINT64 rdi;
	UINT64 rdx;
	UINT64 rcx;
	UINT64 rbx;
	UINT64 rax;
	UINT64 rflags;
	UINT64 rip;
	UINT64 rsp;
	UINT64 extraInfo1;
	UINT64 extraInfo2;
};

class IMsrInterceptPlugin;
class ICpuidInterceptPlugin;
class INpfInterceptPlugin;
class SVMManager;

//SVM 每个核心的虚拟化信息
struct VirtCpuInfo
{
	DECLSPEC_ALIGN(PAGE_SIZE) VMCB guestVmcb;
	DECLSPEC_ALIGN(PAGE_SIZE) VMCB hostVmcb;
	DECLSPEC_ALIGN(PAGE_SIZE) UINT8 hostStatus[PAGE_SIZE];
	DECLSPEC_ALIGN(PAGE_SIZE) struct 
	{
		UINT32 isInVirtualizaion;
		SVMManager* pSvmManager;
		ULONG cpuIdx;
		PVOID pNptPageTablePa;
	} otherInfo;
	DECLSPEC_ALIGN(PAGE_SIZE) struct
	{
		GenericRegisters genericRegisters1;
		GenericRegisters genericRegisters2;
	} regsBackup;
	DECLSPEC_ALIGN(PAGE_SIZE) UINT8 stack1[KERNEL_STACK_SIZE];
	DECLSPEC_ALIGN(PAGE_SIZE) UINT8 stack2[KERNEL_STACK_SIZE];
};

//MSR拦截插件
class IMsrInterceptPlugin
{
public:
	//设置拦截的msr寄存器
	virtual void SetMsrPremissionMap(RTL_BITMAP& bitmap) = 0;
	//处理拦截的msr读取，true代表已经处理，false代表未处理
	virtual bool HandleMsrImterceptRead(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters, 
										PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr, 
										UINT32 msrNum) = 0;
	//处理拦截的Mst写入，true代表已经处理，false代表未处理
	virtual bool HandleMsrInterceptWrite(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters,
										 PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr, 
										 UINT32 msrNum) = 0;

	#pragma code_seg()
	virtual ~IMsrInterceptPlugin() {}
};

//MSR 备份恢复插件（用于在VMM加载和退出时备份和加载没有在VMCB中存在guest版本的msr）
class IMsrBackupRestorePlugin
{
public:
	//加载和保存guest的MSR
	virtual void LoadGuestMsrForCpu(UINT32 cpuIdx) = 0;
	virtual void SaveGuestMsrForCpu(UINT32 cpuIdx) = 0;

	//加载和保存host的MSR
	virtual void LoadHostMsrForCpu(UINT32 cpuIdx) = 0;
	virtual void SaveHostMsrForCpu(UINT32 cpuIdx) = 0;

	virtual ~IMsrBackupRestorePlugin() {}
};

//CPUID拦截插件
class ICpuidInterceptPlugin
{
public:
	//处理拦截的cpuid指令，true代表已经处理，false代表未处理
	virtual bool HandleCpuid(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters,
							 PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr) = 0;
	#pragma code_seg()
	virtual ~ICpuidInterceptPlugin() {}
};

//NPF拦截插件
class INpfInterceptPlugin
{
public:
	//处理拦截的NPF事件，true代表已经处理，false代表未处理
	virtual bool HandleNpf(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters,
		PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr) = 0;
	#pragma code_seg()
	virtual ~INpfInterceptPlugin() {}
};

//BP拦截插件
class IBreakprointInterceptPlugin
{
public:
	//处理拦截的BP事件，true代表已经处理，false代表未处理
	virtual bool HandleBreakpoint(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters,
		PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr) = 0;
	#pragma code_seg()
	virtual ~IBreakprointInterceptPlugin() {}
};

//UD拦截插件
class IInvalidOpcodeInterceptPlugin
{
public:
	//处理拦截的UD事件，true代表已经处理，false代表未处理
	virtual bool HandleInvalidOpcode(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters,
		PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr) = 0;
	#pragma code_seg()
	virtual ~IInvalidOpcodeInterceptPlugin() {}
};

//DE拦截插件
class ISingleStepInterceptPlugin
{
public:
	//处理拦截的DE事件，true代表已经处理，false代表未处理
	virtual bool HandleSignleStep(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters, 
		PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr) = 0;
	#pragma code_seg()
	virtual ~ISingleStepInterceptPlugin() {}
};

//NPT页表提供接口
class INCr3Provider
{
public:
	//根据CPUID获取对应的NCR3物理地址
	virtual PVOID GetNCr3ForCore(UINT32 cpuIdx) = 0;
	#pragma code_seg()
	virtual ~INCr3Provider() {}
};

//VMCB的msrpmBasePA指向的内容，全局只需要一份
//这个类负责初始化该资源
class MsrPremissionsMapManager : IManager
{
	PVOID pMsrPremissionsMapVirtAddr;
	PVOID pMsrPremissionsMapPhyAddr;
	IMsrInterceptPlugin* pMsrInterceptPlugin;
public:
	#pragma code_seg()
	MsrPremissionsMapManager()
		: pMsrPremissionsMapVirtAddr(NULL), pMsrPremissionsMapPhyAddr(NULL), pMsrInterceptPlugin(NULL)
	{}
	void SetPlugin(IMsrInterceptPlugin* _pMsrInterceptPlugin) { PAGED_CODE(); pMsrInterceptPlugin = _pMsrInterceptPlugin; }
	virtual NTSTATUS Init() override;
	#pragma code_seg()
	PTR_TYPE GetPhyAddress() const { return (PTR_TYPE)pMsrPremissionsMapPhyAddr; }
	#pragma code_seg()
	bool IsInited() const { return pMsrPremissionsMapVirtAddr != NULL; }
	virtual void Deinit() override;
	#pragma code_seg()
	virtual ~MsrPremissionsMapManager() { MsrPremissionsMapManager::Deinit(); }
};

enum SVMStatus
{
	//初始值，不表示任何信息
	SVMS_UNUSED = 0x0,
	//非AMDCPU
	SVMS_NONAMDCPU = 0x1,
	//CPU支持SVM
	SVMS_SUPPORTED = 0x2,
	//SVM已经启用
	SVMS_ENABLED = 0x4,
	//SVM就绪（未被其他虚拟化软件占用）
	SVMS_READY = 0x8,
	//NPT可用
	SVMS_NPT_ENABLED = 0x10
};

extern "C" void VmExitHandler(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters, PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr);

class SVMManager : public IManager
{
	VirtCpuInfo** pVirtCpuInfo;
	UINT32 cpuCnt;
	MsrPremissionsMapManager msrPremissionMap;
	IMsrInterceptPlugin* pMsrInterceptPlugin;
	IMsrBackupRestorePlugin* pMsrBackupRestorePlugin;
	ICpuidInterceptPlugin* pCpuIdInterceptPlugin;
	INpfInterceptPlugin* pNpfInterceptPlugin;
	IBreakprointInterceptPlugin* pBreakpointInterceptPlugin;
	ISingleStepInterceptPlugin* pSingleStepInterceptPlugin;
	INCr3Provider* pNCr3Provider;
	IInvalidOpcodeInterceptPlugin* pInvalidOpcodeInterceptPlugin;
	bool enableSce;

	friend void VmExitHandler(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters, PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr);
	
public:
	NTSTATUS EnterVirtualization();
	void LeaveVirtualization();
	//请勿调用该函数，这个函数由VMM自动调用
	void VmExitHandler(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters, PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr);
	#pragma code_seg("PAGE")
	SVMManager() : pVirtCpuInfo(NULL), cpuCnt(0), pMsrInterceptPlugin(NULL), pCpuIdInterceptPlugin(NULL), pNpfInterceptPlugin(NULL), pNCr3Provider(NULL), pBreakpointInterceptPlugin(NULL), pInvalidOpcodeInterceptPlugin(NULL), pSingleStepInterceptPlugin(NULL), enableSce(true), pMsrBackupRestorePlugin(NULL) { PAGED_CODE(); }
	#pragma code_seg("PAGE")
	void SetMsrInterceptPlugin(IMsrInterceptPlugin* _pMsrInterceptPlugin) { PAGED_CODE(); pMsrInterceptPlugin = _pMsrInterceptPlugin; }
	#pragma code_seg("PAGE")
	IMsrInterceptPlugin* GetMsrInterceptPlugin() { PAGED_CODE(); return pMsrInterceptPlugin; }
	#pragma code_seg("PAGE")
	void SetCpuIdInterceptPlugin(ICpuidInterceptPlugin* _pCpuIdInterceptPlugin) { PAGED_CODE(); pCpuIdInterceptPlugin = _pCpuIdInterceptPlugin; }
	#pragma code_seg("PAGE")
	ICpuidInterceptPlugin* GetCpuidInterceptPlugin() { PAGED_CODE(); return pCpuIdInterceptPlugin; }
	#pragma code_seg("PAGE")
	void SetNpfInterceptPlugin(INpfInterceptPlugin* _pNpfInterrceptPlugin) { PAGED_CODE(); pNpfInterceptPlugin = _pNpfInterrceptPlugin; }
	#pragma code_seg("PAGE")
	INpfInterceptPlugin* GetNpfnterceptPlugin() { PAGED_CODE(); return pNpfInterceptPlugin; }
	#pragma code_seg("PAGE")
	void SetNCr3Provider(INCr3Provider* _provider) { PAGED_CODE(); pNCr3Provider = _provider; }
	#pragma code_seg("PAGE")
	INCr3Provider* GetCr3Provider() { PAGED_CODE(); return pNCr3Provider; }
	#pragma code_seg("PAGE")
	void SetBreakpointPlugin(IBreakprointInterceptPlugin* _pBreakpointInterceptPlugin) { PAGED_CODE(); pBreakpointInterceptPlugin = _pBreakpointInterceptPlugin; }
	#pragma code_seg("PAGE")
	IBreakprointInterceptPlugin* GetBreakpointPlugin() { PAGED_CODE(); return pBreakpointInterceptPlugin; }
	#pragma code_seg("PAGE")
	void SetInvalidOpcodePlugin(IInvalidOpcodeInterceptPlugin* _pInvalidOpcodeInterceptPlugin) { PAGED_CODE(); pInvalidOpcodeInterceptPlugin = _pInvalidOpcodeInterceptPlugin; }
	#pragma code_seg("PAGE")
	IInvalidOpcodeInterceptPlugin* GetInvalidOpcodePlugin() { PAGED_CODE(); return pInvalidOpcodeInterceptPlugin; }
	#pragma code_seg("PAGE")
	void SetSingleStepPlugin(ISingleStepInterceptPlugin* _pDebugInterceptPlugin) { PAGED_CODE(); pSingleStepInterceptPlugin = _pDebugInterceptPlugin; }
	#pragma code_seg("PAGE")
	ISingleStepInterceptPlugin* GetSingleStepPlugin() { PAGED_CODE(); return pSingleStepInterceptPlugin; }
	#pragma code_seg("PAGE")
	void SetMsrBackupRestorePlugin(IMsrBackupRestorePlugin* _pMsrHookPlugin) { PAGED_CODE(); pMsrBackupRestorePlugin = _pMsrHookPlugin; }
	#pragma code_seg("PAGE")
	IMsrBackupRestorePlugin* GetMsrBackupRestorePlugin() { PAGED_CODE(); return pMsrBackupRestorePlugin; }
	#pragma code_seg("PAGE")
	void EnanbleSce(bool enable) { PAGED_CODE(); enableSce = enable; }
	#pragma code_seg("PAGE")
	bool IsSceEnabled() { PAGED_CODE(); return enableSce; }
	static SVMStatus CheckSVM();	
	virtual NTSTATUS Init() override;
	virtual void Deinit() override;
	#pragma code_seg("PAGE")
	virtual ~SVMManager() { PAGED_CODE(); SVMManager::Deinit(); }
};

//段选择器的attribute
//照抄https://github.com/tandasat/SimpleSvm
typedef struct _SEGMENT_ATTRIBUTE
{
	union
	{
		UINT16 AsUInt16;
		struct
		{
			UINT16 Type : 4;        // [0:3]
			UINT16 System : 1;      // [4]
			UINT16 Dpl : 2;         // [5:6]
			UINT16 Present : 1;     // [7]
			UINT16 Avl : 1;         // [8]
			UINT16 LongMode : 1;    // [9]
			UINT16 DefaultBit : 1;  // [10]
			UINT16 Granularity : 1; // [11]
			UINT16 Reserved1 : 4;   // [12:15]
		} Fields;
	};
} SEGMENT_ATTRIBUTE, * PSEGMENT_ATTRIBUTE;

typedef SEGMENT_ATTRIBUTE SegmentAttribute;

//获取段描述
SegmentAttribute GetSegmentAttribute(_In_ UINT16 SegmentSelector, _In_ ULONG_PTR GdtBase);
//获取段基地址
UINT64 GetSegmentBaseAddress(_In_ UINT16 SegmentSelector, _In_ ULONG_PTR GdtBase);
//获取段limit，后面加一个2是防止和系统函数冲突，可以直接使用系统函数
UINT32 GetSegmentLimit2(_In_ UINT16 SegmentSelector, _In_ ULONG_PTR GdtBase);

//一系列汇编函数
//源代码在SVM_asm.asm里面
//主要都是寄存器读取操作
extern "C" void _mysgdt(UINT64* pBase, UINT16* pLImit);
extern "C" void _mysidt(UINT64* pBase, UINT16* pLImit);
extern "C" void _mysldt(UINT16* pSelector);
extern "C" void _mystr(UINT16* pSelector);
extern "C" UINT16 _cs_selector();
extern "C" UINT16 _ds_selector();
extern "C" UINT16 _es_selector();
extern "C" UINT16 _fs_selector();
extern "C" UINT16 _gs_selector();
extern "C" UINT16 _ss_selector();

//用于备份和还原寄存器上下文
extern "C" void _save_or_load_regs(GenericRegisters* pRegisters);
//执行vmrun相关操作
extern "C" void _run_svm_vmrun(VirtCpuInfo* pVirtCpuInfo, PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr, PVOID pStack);

/*
	(virtCpuInfo)->guestVmcb.statusFields.gdtr.base = gdtrBase;
	(virtCpuInfo)->guestVmcb.statusFields.gdtr.limit = gdtrLimit;
	(virtCpuInfo)->guestVmcb.statusFields.idtr.base = idtrBase;
	(virtCpuInfo)->guestVmcb.statusFields.idtr.limit = idtrLimit;

	//X64 代码段和数据段的base和limit是无效的（FS,GS除外）
	//base 强制为 0（强制平坦段）
	//这里还是读取了原始的base和limit
	(virtCpuInfo)->guestVmcb.statusFields.cs.selector = _cs_selector();
	(virtCpuInfo)->guestVmcb.statusFields.cs.base = GetSegmentBaseAddress(_cs_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.cs.limit = GetSegmentLimit2(_cs_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.cs.attrib = GetSegmentAttribute(_cs_selector(), gdtrBase).AsUInt16;

	(virtCpuInfo)->guestVmcb.statusFields.ds.selector = _ds_selector();
	(virtCpuInfo)->guestVmcb.statusFields.ds.base = GetSegmentBaseAddress(_ds_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.ds.limit = GetSegmentLimit2(_ds_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.ds.attrib = GetSegmentAttribute(_ds_selector(), gdtrBase).AsUInt16;

	(virtCpuInfo)->guestVmcb.statusFields.es.selector = _es_selector();
	(virtCpuInfo)->guestVmcb.statusFields.es.base = GetSegmentBaseAddress(_es_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.es.limit = GetSegmentLimit2(_es_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.es.attrib = GetSegmentAttribute(_es_selector(), gdtrBase).AsUInt16;

	(virtCpuInfo)->guestVmcb.statusFields.ss.selector = _ss_selector();
	(virtCpuInfo)->guestVmcb.statusFields.ss.base = GetSegmentBaseAddress(_ss_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.ss.limit = GetSegmentLimit2(_ss_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.ss.attrib = GetSegmentAttribute(_ss_selector(), gdtrBase).AsUInt16;

	//下面的这一组信息可以使用vmsave指令直接获取
	//这里为了研究原理手动获取
	//*************************************** BEGIN ***************************************

	(virtCpuInfo)->guestVmcb.statusFields.fs.selector = _fs_selector();
	(virtCpuInfo)->guestVmcb.statusFields.fs.limit = GetSegmentLimit2(_fs_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.fs.attrib = GetSegmentAttribute(_fs_selector(), gdtrBase).AsUInt16;

	(virtCpuInfo)->guestVmcb.statusFields.gs.selector = _gs_selector();
	(virtCpuInfo)->guestVmcb.statusFields.gs.limit = GetSegmentLimit2(_gs_selector(), gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.gs.attrib = GetSegmentAttribute(_gs_selector(), gdtrBase).AsUInt16;

	//对于TR LDTR base limit 依然有效
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.selector = ldtrSelector;
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.base = GetSegmentBaseAddress(ldtrSelector, gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.limit = GetSegmentLimit2(ldtrSelector, gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.attrib = GetSegmentAttribute(ldtrSelector, gdtrBase).AsUInt16;

	(virtCpuInfo)->guestVmcb.statusFields.tr.selector = trSelector;
	(virtCpuInfo)->guestVmcb.statusFields.tr.base = GetSegmentBaseAddress(trSelector, gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.tr.limit = GetSegmentLimit2(trSelector, gdtrBase);
	(virtCpuInfo)->guestVmcb.statusFields.tr.attrib = GetSegmentAttribute(trSelector, gdtrBase).AsUInt16;

	//FSBase GSBase KenrelGSBase 可以不为0 但是是放在MSR寄存器里面的
	//IA32_MSR_FS_BASE（下标0xC0000100）
	//IA32_MSR_GS_BASE（下标0xC0000101）
	//IA32_MSR_KERNEL_GS_BASE（下标0xC0000102）

	(virtCpuInfo)->guestVmcb.statusFields.fs.base = __readmsr(IA32_MSR_FS_BASE);
	(virtCpuInfo)->guestVmcb.statusFields.gs.base = __readmsr(IA32_MSR_GS_BASE);
	(virtCpuInfo)->guestVmcb.statusFields.kernelGsBase = __readmsr(IA32_MSR_KERNEL_GS_BASE);

	//对于32位系统才需要填充 SYSENTER_CS SYSENTER_ESP SYSENTER_EIP

	//(virtCpuInfo)->guestVmcb.statusFields.sysenterCs = __readmsr(IA32_MSR_SYSENTER_CS);
	//(virtCpuInfo)->guestVmcb.statusFields.sysenterEsp = __readmsr(IA32_MSR_SYSENTER_ESP);
	//(virtCpuInfo)->guestVmcb.statusFields.sysenterEip = __readmsr(IA32_MSR_SYSENTER_EIP);

	(virtCpuInfo)->guestVmcb.statusFields.star = __readmsr(IA32_MSR_STAR);
	(virtCpuInfo)->guestVmcb.statusFields.lstar = __readmsr(IA32_MSR_LSTAR);
	(virtCpuInfo)->guestVmcb.statusFields.cstar = __readmsr(IA32_MSR_CSTAR);
	(virtCpuInfo)->guestVmcb.statusFields.sfmask = __readmsr(IA32_MSR_SF_MASK);

	//*************************************** END ***************************************

	//填充 VMCB EFER 的 EFER 值中SVME位必须为1，否则vmrun会失败
	(virtCpuInfo)->guestVmcb.statusFields.efer = __readmsr(IA32_MSR_EFER);

	//需要时禁用syscall和sysret
	if (!enableSce)
	(virtCpuInfo)->guestVmcb.statusFields.efer &= ~(1 << SCE_ENABLE_OFFSET);

	(virtCpuInfo)->guestVmcb.statusFields.cr0 = __readcr0();
	(virtCpuInfo)->guestVmcb.statusFields.cr2 = __readcr2();
	(virtCpuInfo)->guestVmcb.statusFields.cr3 = __readcr3();
	(virtCpuInfo)->guestVmcb.statusFields.cr4 = __readcr4();
	//设置rax为GenericRegisters的地址，这样__save_or_load_regs就能通过rax访问到GenericRegisters并恢复寄存器
	(virtCpuInfo)->guestVmcb.statusFields.rax = (PTR_TYPE)&registerBackup;
	(virtCpuInfo)->guestVmcb.statusFields.rflags = registerBackup.rflags;
	(virtCpuInfo)->guestVmcb.statusFields.rsp = registerBackup.rsp;
	(virtCpuInfo)->guestVmcb.statusFields.rip = registerBackup.rip;
	(virtCpuInfo)->guestVmcb.statusFields.gPat = __readmsr(IA32_MSR_PAT);

	(virtCpuInfo)->guestVmcb.statusFields.cpl = _cs_selector() & 0x3;

	(virtCpuInfo)->hostVmcb.statusFields = (virtCpuInfo)->guestVmcb.statusFields;
*/

#define SAVE_GUEST_STATUS_FROM_REGS(virtCpuInfo, rax_val, rflags_val, rsp_val, rip_val)												\
																																	\
	UINT64 gdtrBase = 0, idtrBase = 0;																								\
	UINT16 gdtrLimit = 0, idtrLimit = 0;																							\
	UINT16 trSelector = 0, ldtrSelector = 0;																						\
	_mysgdt(&gdtrBase, &gdtrLimit);																									\
	_mysidt(&idtrBase, &idtrLimit);																									\
	_mystr(&trSelector);																											\
	_mysldt(&ldtrSelector);																											\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.gdtr.base = gdtrBase;																		\
	(virtCpuInfo)->guestVmcb.statusFields.gdtr.limit = gdtrLimit;																	\
	(virtCpuInfo)->guestVmcb.statusFields.idtr.base = idtrBase;																		\
	(virtCpuInfo)->guestVmcb.statusFields.idtr.limit = idtrLimit;																	\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.cs.selector = _cs_selector();																\
	(virtCpuInfo)->guestVmcb.statusFields.cs.base = GetSegmentBaseAddress(_cs_selector(), gdtrBase);								\
	(virtCpuInfo)->guestVmcb.statusFields.cs.limit = GetSegmentLimit2(_cs_selector(), gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.cs.attrib = GetSegmentAttribute(_cs_selector(), gdtrBase).AsUInt16;						\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.ds.selector = _ds_selector();																\
	(virtCpuInfo)->guestVmcb.statusFields.ds.base = GetSegmentBaseAddress(_ds_selector(), gdtrBase);								\
	(virtCpuInfo)->guestVmcb.statusFields.ds.limit = GetSegmentLimit2(_ds_selector(), gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.ds.attrib = GetSegmentAttribute(_ds_selector(), gdtrBase).AsUInt16;						\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.es.selector = _es_selector();																\
	(virtCpuInfo)->guestVmcb.statusFields.es.base = GetSegmentBaseAddress(_es_selector(), gdtrBase);								\
	(virtCpuInfo)->guestVmcb.statusFields.es.limit = GetSegmentLimit2(_es_selector(), gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.es.attrib = GetSegmentAttribute(_es_selector(), gdtrBase).AsUInt16;						\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.ss.selector = _ss_selector();																\
	(virtCpuInfo)->guestVmcb.statusFields.ss.base = GetSegmentBaseAddress(_ss_selector(), gdtrBase);								\
	(virtCpuInfo)->guestVmcb.statusFields.ss.limit = GetSegmentLimit2(_ss_selector(), gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.ss.attrib = GetSegmentAttribute(_ss_selector(), gdtrBase).AsUInt16;						\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.fs.selector = _fs_selector();																\
	(virtCpuInfo)->guestVmcb.statusFields.fs.limit = GetSegmentLimit2(_fs_selector(), gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.fs.attrib = GetSegmentAttribute(_fs_selector(), gdtrBase).AsUInt16;						\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.gs.selector = _gs_selector();																\
	(virtCpuInfo)->guestVmcb.statusFields.gs.limit = GetSegmentLimit2(_gs_selector(), gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.gs.attrib = GetSegmentAttribute(_gs_selector(), gdtrBase).AsUInt16;						\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.selector = ldtrSelector;																\
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.base = GetSegmentBaseAddress(ldtrSelector, gdtrBase);								\
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.limit = GetSegmentLimit2(ldtrSelector, gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.ldtr.attrib = GetSegmentAttribute(ldtrSelector, gdtrBase).AsUInt16;						\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.tr.selector = trSelector;																	\
	(virtCpuInfo)->guestVmcb.statusFields.tr.base = GetSegmentBaseAddress(trSelector, gdtrBase);									\
	(virtCpuInfo)->guestVmcb.statusFields.tr.limit = GetSegmentLimit2(trSelector, gdtrBase);										\
	(virtCpuInfo)->guestVmcb.statusFields.tr.attrib = GetSegmentAttribute(trSelector, gdtrBase).AsUInt16;							\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.fs.base = __readmsr(IA32_MSR_FS_BASE);													\
	(virtCpuInfo)->guestVmcb.statusFields.gs.base = __readmsr(IA32_MSR_GS_BASE);													\
	(virtCpuInfo)->guestVmcb.statusFields.kernelGsBase = __readmsr(IA32_MSR_KERNEL_GS_BASE);										\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.star = __readmsr(IA32_MSR_STAR);															\
	(virtCpuInfo)->guestVmcb.statusFields.lstar = __readmsr(IA32_MSR_LSTAR);														\
	(virtCpuInfo)->guestVmcb.statusFields.cstar = __readmsr(IA32_MSR_CSTAR);														\
	(virtCpuInfo)->guestVmcb.statusFields.sfmask = __readmsr(IA32_MSR_SF_MASK);														\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.efer = __readmsr(IA32_MSR_EFER);															\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.cr0 = __readcr0();																		\
	(virtCpuInfo)->guestVmcb.statusFields.cr2 = __readcr2();																		\
	(virtCpuInfo)->guestVmcb.statusFields.cr3 = __readcr3();																		\
	(virtCpuInfo)->guestVmcb.statusFields.cr4 = __readcr4();																		\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.rax = (UINT64)(rax_val);																	\
	(virtCpuInfo)->guestVmcb.statusFields.rflags = (UINT64)(rflags_val);															\
	(virtCpuInfo)->guestVmcb.statusFields.rsp = (UINT64)(rsp_val);																	\
	(virtCpuInfo)->guestVmcb.statusFields.rip = (UINT64)(rip_val);																	\
	(virtCpuInfo)->guestVmcb.statusFields.gPat = __readmsr(IA32_MSR_PAT);															\
																																	\
	(virtCpuInfo)->guestVmcb.statusFields.cpl = _cs_selector() & 0x3;

#endif
