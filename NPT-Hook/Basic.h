#ifndef BASIC_H
#define BASIC_H

//表明是64位Windows，目前仅开发64位版本
#define WINDOWS_X64
//控制内存分配函数的使用
//具体见https://learn.microsoft.com/zh-cn/windows-hardware/drivers/ddi/wdm/nf-wdm-exallocatepoolwithtag
//#define _BUILD_WIN_2004

#ifndef _BUILD_WIN_2004
#pragma warning(disable : 4996)
#endif

//头文件包含，一些基础定义
#include <ntddk.h>
extern "C" {
	#include <xed/xed-interface.h>
}

//一些类型别名
typedef unsigned char UINT8;
typedef char SINT8;
typedef unsigned short UINT16;
typedef short SINT16;
typedef unsigned int UINT32;
typedef int SINT32;
typedef unsigned long long UINT64;
typedef long long SINT64;
#if defined(WINDOWS_X64)
typedef UINT64 SIZE_TYPE;
typedef UINT64 PTR_TYPE;
#elif defined(WINDOWS_X86)
typedef UINT32 SIZE_TYPE;
typedef UINT32 PTR_TYPE;
#endif

//设置tag
#define C_TO_U32(c) ((UINT32)(c))
#define MAKE_TAG(c1,c2,c3,c4) (UINT32)((C_TO_U32(c4) << 24) + (C_TO_U32(c3) << 16) + (C_TO_U32(c2) << 8) + C_TO_U32(c1))

//全局placement new 和 placement delete
void* operator new(size_t, void* pObj);
void operator delete(void*, UINT64);

//调用构造函数初始化内存
#pragma code_seg()
template<typename T, typename ...Args>
void CallConstructor(T* pObj, Args&& ...args)
{
	new (pObj) T(args...);
}

//对指定内存的对象进行析构
#pragma code_seg()
template<typename T>
void CallDestroyer(T* pObj)
{
	delete (0, pObj);
}

//在每一个CPU核心上都运行一次指定的可执行对象
#pragma code_seg("PAGE")
template<typename Func, typename ...Args>
NTSTATUS RunOnEachCore(UINT32 startCoreIdx, UINT32 endCoreIdx, Func&& func, Args&& ...args)
{
	PAGED_CODE();
	NTSTATUS status = STATUS_SUCCESS;
	PROCESSOR_NUMBER processorNum = {};
	GROUP_AFFINITY affinity = {}, oldAffinity = {};

	for (UINT32 cpuIdx = startCoreIdx; cpuIdx < endCoreIdx; ++cpuIdx)
	{
		status = KeGetProcessorNumberFromIndex(cpuIdx, &processorNum);
		if (!NT_SUCCESS(status))
			break;

		affinity = {};
		affinity.Group = processorNum.Group;
		affinity.Mask = 1ULL << processorNum.Number;

		KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);

		status = func(cpuIdx, args...);

		KeRevertToUserGroupAffinityThread(&oldAffinity);

		if (!NT_SUCCESS(status))
			break;
	}

	return status;
}

//对于驱动各个组件的一个抽象
class IManager
{
public:
	virtual NTSTATUS Init() = 0;
	virtual void Deinit() = 0;
	#pragma code_seg()
	virtual ~IManager() {}
};

//帮助函数，取数组的元素数目
#pragma code_seg()
template<typename T, SIZE_TYPE n>
SIZE_TYPE GetArrayElementCnt(T (&)[n])
{
	return n;
}

//封装一下内存分配函数
PVOID AllocNonPagedMem(SIZE_TYPE byteCnt, ULONG tag);
void FreeNonPagedMem(PVOID pMem, ULONG tag);
PVOID AllocPagedMem(SIZE_TYPE byteCnt, ULONG tag);
void FreePagedMem(PVOID pMem, ULONG tag);
PVOID AllocContiguousMem(SIZE_TYPE byteCnt, ULONG tag);
void FreeContigousMem(PVOID pMem, ULONG tag);
PVOID AllocExecutableNonPagedMem(SIZE_TYPE byteCnt, ULONG tag);
void FreeExecutableNonPagedMem(PVOID pMem, ULONG tag);

//轮询等待对象知道成功，这样设计是为了可以在DISPATCH_LEVEL下可以等待
void WaitForSignleObjectInfinte(PVOID Object, KWAIT_REASON WaitReason, KPROCESSOR_MODE WaitMode, BOOLEAN Alertable);

enum MemType
{
	NonPaged,
	Paged,
	ContigousMem,
};

//一些无效值
constexpr PHYSICAL_ADDRESS HIGHEST_PHY_ADDR = { (ULONG)-1,-1 };
constexpr PTR_TYPE INVALID_ADDR = (PTR_TYPE)-1;
constexpr SIZE_TYPE INVALID_INDEX = (SIZE_TYPE)-1;

//一些CPU相关的常量，比如MSR 编号，某标志的偏移位数
constexpr UINT32 IA32_MSR_EFER = 0xc0000080;
constexpr UINT32 IA32_MSR_PAT = 0x00000277;
constexpr UINT32 IA32_MSR_FS_BASE = 0xC0000100;
constexpr UINT32 IA32_MSR_GS_BASE = 0xC0000101;
constexpr UINT32 IA32_MSR_KERNEL_GS_BASE = 0xC0000102;
constexpr UINT32 IA32_MSR_STAR = 0xC0000081;
constexpr UINT32 IA32_MSR_LSTAR = 0xC0000082; 
constexpr UINT32 IA32_MSR_CSTAR = 0xC0000083;
constexpr UINT32 IA32_MSR_SF_MASK = 0xC0000084;
constexpr UINT32 IA32_MSR_SYSENTER_CS = 0x174;
constexpr UINT32 IA32_MSR_SYSENTER_ESP = 0x175;
constexpr UINT32 IA32_MSR_SYSENTER_EIP = 0x176;
constexpr UINT32 IA32_MSR_SVM_MSR_VM_HSAVE_PA = 0xC0010117;
constexpr UINT32 IA32_MSR_VM_CR = 0xC0010114;
constexpr UINT32 IA32_MSR_APIC_BASE = 0x0000001b;
constexpr UINT32 EFER_SVME_OFFSET = 12;
constexpr UINT32 CPUID_FN_80000001_ECX_SVM_OFFSET = 2;
constexpr UINT32 VM_CR_SVMDIS_OFFSET = 4;
constexpr UINT32 NPT_ENABLE_OFFSET = 0;
constexpr UINT32 SCE_ENABLE_OFFSET = 0;
constexpr UINT32 CPUID_FN_SVM_FEATURE = 0x80000001;
constexpr UINT32 CPUID_FN_NPT_FEATURE = 0x8000000a;
constexpr UINT32 EFLAGS_RF_OFFSET = 16;
constexpr UINT32 EFLAGS_TF_OFFSET = 8;
constexpr UINT32 EFLAGS_IF_OFFSET = 9;
constexpr UINT32 BP_EXCEPTION_VECTOR_INDEX = 3;
constexpr UINT32 UD_EXCEPTION_VECTOR_INDEX = 6;
constexpr UINT32 DB_EXCEPTION_VECTOR_INDEX = 1;

//类似C++11中的std::vector，内核模式的变长数组
template<typename ElementType, UINT32 allocTag, MemType memType = NonPaged>
class KernelVector
{
	ElementType* pData;
	SIZE_TYPE length;
	SIZE_TYPE capacity;

	PVOID (*pMemAlloc)(SIZE_TYPE byteCnt, ULONG tag);
	void (*pMemFree)(PVOID pMem, ULONG tag);

public:
	KernelVector();
	~KernelVector();

	KernelVector(KernelVector&& container);
	KernelVector& operator=(KernelVector&& container);

	KernelVector(const KernelVector& container);
	KernelVector& operator=(const KernelVector& container);

	void PushBack(ElementType e);
	void EmplaceBack(ElementType&& e);
	ElementType PopBack();
	const ElementType& operator[](SIZE_TYPE idx) const;
	ElementType& operator[](SIZE_TYPE idx);
	void Insert(ElementType e, SIZE_TYPE idx);
	void Remove(SIZE_TYPE idx);
	SIZE_TYPE Length() const;
	SIZE_TYPE Capacity() const;
	void SetCapacity(SIZE_TYPE newCapacity);
	void Clear();
};

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline KernelVector<ElementType, allocTag, memType>::KernelVector() : pData(NULL), length(0), capacity(0)
{
	//根据内存类型，选择不同的内存分配释放函数

	if constexpr (memType == MemType::NonPaged)
	{
		pMemAlloc = AllocNonPagedMem;
		pMemFree = FreeNonPagedMem;
	}
	else if constexpr (memType == MemType::Paged)
	{
		pMemAlloc = AllocPagedMem;
		pMemFree = FreePagedMem;
	}
	else if constexpr (memType = MemType::ContigousMem)
	{
		pMemAlloc = AllocContiguousMem;
		pMemFree = FreeContigousMem;
	}
	else
	{
		__debugbreak();
		KeBugCheck(DRIVER_INVALID_CRUNTIME_PARAMETER);
	}
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline KernelVector<ElementType, allocTag, memType>::~KernelVector()
{
	if (pData != NULL)
	{
		for (SIZE_TYPE idx = 0; idx < length; ++idx)
			CallDestroyer(pData + idx);

		pMemFree(pData, allocTag);

		pData = NULL;
	}
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline KernelVector<ElementType, allocTag, memType>::KernelVector(KernelVector&& container) : KernelVector()
{
	*this = static_cast<KernelVector<ElementType, allocTag, memType>&&>(container);
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline KernelVector<ElementType, allocTag, memType>& KernelVector<ElementType, allocTag, memType>::operator=(KernelVector&& container)
{
	if (&container == this)
		return *this;

	this->~KernelVector();

	pData = container.pData;
	length = container.length;
	capacity = container.capacity;
	container.pData = NULL;
	container.length = 0;
	container.capacity = 0;

	return *this;
}

template<typename ElementType, UINT32 allocTag, MemType memType>
inline KernelVector<ElementType, allocTag, memType>::KernelVector(const KernelVector& container) : KernelVector()
{
	*this = container;
}

template<typename ElementType, UINT32 allocTag, MemType memType>
inline KernelVector<ElementType, allocTag, memType>& KernelVector<ElementType, allocTag, memType>::operator=(const KernelVector& container)
{
	if (&container == this)
		return *this;

	Clear();

	SetCapacity(container.Capacity());

	for (SIZE_TYPE idx = 0; idx < container.Length(); ++idx)
		CallConstructor(pData + idx, container[idx]);

	length = container.Length();

	return *this;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline void KernelVector<ElementType, allocTag, memType>::PushBack(ElementType e)
{
	if (length == capacity)
		SetCapacity(!length ? 50 : length * 2);
	pData[length++] = e;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline void KernelVector<ElementType, allocTag, memType>::EmplaceBack(ElementType&& e)
{
	if (length == capacity)
		SetCapacity(!length ? 50 : length * 2);
	CallConstructor(&pData[length]);
	pData[length++] = static_cast<ElementType&&>(e);
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline ElementType KernelVector<ElementType, allocTag, memType>::PopBack()
{
	ElementType result = static_cast<ElementType&&>(pData[--length]);
	return result;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline const ElementType& KernelVector<ElementType, allocTag, memType>::operator[](SIZE_TYPE idx) const
{
	if (idx < Length())
	{
		return pData[idx];
	}
	else
	{
		__debugbreak();
		KeBugCheck(MEMORY_MANAGEMENT);
	}
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline ElementType& KernelVector<ElementType, allocTag, memType>::operator[](SIZE_TYPE idx)
{
	if (idx < Length())
	{
		return pData[idx];
	}
	else
	{
		__debugbreak();
		KeBugCheck(MEMORY_MANAGEMENT);
	}
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline void KernelVector<ElementType, allocTag, memType>::Insert(ElementType e, SIZE_TYPE idx)
{
	if (idx >= Length())
	{
		__debugbreak();
		KeBugCheck(MEMORY_MANAGEMENT);
	}

	if (length == capacity)
		SetCapacity(length + 50);

	for(SIZE_T idx2 = length; idx2 > idx; --idx2)
		pData[idx2] = static_cast<ElementType&&>(pData[idx2 - 1]);

	pData[idx] = e;

	++length;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline void KernelVector<ElementType, allocTag, memType>::Remove(SIZE_TYPE idx)
{
	if (idx >= Length())
	{
		__debugbreak();
		KeBugCheck(MEMORY_MANAGEMENT);
	}

	for (SIZE_TYPE idx2 = idx; idx2 < Length() - 1; ++idx2)
		pData[idx2] = static_cast<ElementType&&>(pData[idx2 + 1]);

	--length;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline SIZE_TYPE KernelVector<ElementType, allocTag, memType>::Length() const
{
	return length;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline SIZE_TYPE KernelVector<ElementType, allocTag, memType>::Capacity() const
{
	return capacity;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline void KernelVector<ElementType, allocTag, memType>::SetCapacity(SIZE_TYPE newCapacity)
{
	SIZE_TYPE copyLength = newCapacity > capacity ? capacity : newCapacity;

	ElementType* pNewData = (ElementType*)pMemAlloc(newCapacity * sizeof(ElementType), allocTag);

	if (pNewData == NULL)
	{
		__debugbreak();
		KeBugCheck(MEMORY_MANAGEMENT);
	}

	for (SIZE_TYPE idx = 0; idx < copyLength; ++idx)
		pNewData[idx] = static_cast<ElementType&&>(pData[idx]);

	if (copyLength < capacity)
	{
		for (SIZE_T idx = 0; idx < capacity; ++idx)
			CallDestroyer(pNewData + idx);
	}

	if (pData != NULL)
		pMemFree(pData, allocTag);

	pData = pNewData;
	capacity = newCapacity;
	length = copyLength;
}

#pragma code_seg()
template<typename ElementType, UINT32 allocTag, MemType memType>
inline void KernelVector<ElementType, allocTag, memType>::Clear()
{
	for (SIZE_TYPE idx = 0; idx < length; ++idx)
		CallDestroyer(pData + idx);

	length = 0;
}

//方便为类提供非分页的默认拷贝和移动构造函数和运算符
#define DEFAULT_NONPAGED_COPY_AND_MOVE_FUNCTION_FOR_CLASS(classname)																\
_Pragma("code_seg()")																												\
classname(const classname&) = default;																								\
_Pragma("code_seg()")																												\
classname(classname&&) = default;																									\
_Pragma("code_seg()")																												\
classname& operator=(const classname&) = default;																					\
_Pragma("code_seg()")																												\
classname& operator=(classname&&) = default;

//判断是否为内核地址
constexpr bool IsKernelAddress(PVOID address)
{
	return ((PTR_TYPE)address) & 0xffff000000000000;
}

#endif // !BASIC_H
