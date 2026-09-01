#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include "Basic.h"
#include "SVM.h"

constexpr ULONG PT_TAG = MAKE_TAG('p', 't', 'm', ' ');

/*
//废弃使用的结构
//见https://www.iaik.tugraz.at/teaching/materials/os/tutorials/paging-on-intel-x86-64/
typedef union
{
	UINT64 data;
	struct
	{
		UINT64 present : 1;
		UINT64 writeable : 1;
		UINT64 userAccess : 1;
		UINT64 writeThrough : 1;
		UINT64 cacheDisabled : 1;
		UINT64 accessed : 1;
		UINT64 ignored3 : 1;
		UINT64 size : 1; // must be 0
		UINT64 ignored2 : 4;
		UINT64 pagePpn : 28;
		UINT64 reserved1 : 12; // must be 0
		UINT64 ignored1 : 11;
		UINT64 executionDisabled : 1;
	} fields;
} PageTableLevel4Entry;

typedef union
{
	UINT64 data;
	struct
	{
		UINT64 present : 1;
		UINT64 writeable : 1;
		UINT64 userAccess : 1;
		UINT64 writeThrough : 1;
		UINT64 cacheDisabled : 1;
		UINT64 accessed : 1;
		UINT64 dirty : 1;
		UINT64 size : 1;
		UINT64 global : 1;
		UINT64 ignored2 : 3;
		UINT64 pagePpn : 28;
		UINT64 reserved1 : 12; // must be 0
		UINT64 ignored1 : 11;
		UINT64 executionDisabled : 1;
	} fields;
} PageTableLevel123Entry;
*/

//新的结构，和弃用结构的区别是pagePpn是40位，原因见amd手册。amd手册中reserved1部分也是pagePpn内容，权限位也做了部分修改
typedef union
{
	UINT64 data;
	struct
	{
		UINT64 present : 1;
		UINT64 writeable : 1;
		UINT64 userAccess : 1;
		UINT64 writeThrough : 1;
		UINT64 cacheDisabled : 1;
		UINT64 accessed : 1;
		UINT64 ignored2 : 6;
		UINT64 pagePpn : 40;
		UINT64 ignored1 : 11;
		UINT64 executionDisabled : 1;
	} fields;
} PageTableLevel4Entry;

typedef union
{
	UINT64 data;
	struct
	{
		UINT64 present : 1;
		UINT64 writeable : 1;
		UINT64 userAccess : 1;
		UINT64 writeThrough : 1;
		UINT64 cacheDisabled : 1;
		UINT64 accessed : 1;
		UINT64 dirty : 1;
		UINT64 size : 1;
		UINT64 global : 1;
		UINT64 ignored2 : 3;
		UINT64 pagePpn : 40;
		UINT64 ignored1 : 11;
		UINT64 executionDisabled : 1;
	} fields;
} PageTableLevel123Entry;

//见SimpleSVMHook项目 https://github.com/tandasat/SimpleSvmHook/blob/master/SimpleSvmHook/x86_64.hpp
typedef union
{
	UINT64 data;
	struct
	{
		UINT64 reserved1 : 8;           // [0:7]
		UINT64 bootstrapProcessor : 1;  // [8]
		UINT64 reserved2 : 1;           // [9]
		UINT64 enableX2ApicMode : 1;    // [10]
		UINT64 enableXApicGlobal : 1;   // [11]
		UINT64 apicBase : 24;           // [12:35]
	} fields;
} ApicBase;

//见SimpleSVMHook项目 https://github.com/tandasat/SimpleSvmHook/blob/master/SimpleSvmHook/Svm.hpp
typedef union
{
	UINT64 data;
	struct
	{
		//Vaild
		UINT64 present : 1;                   // [0]
		//Write
		UINT64 writeable : 1;                   // [1]
		//User
		UINT64 userAccesss : 1;                    // [2]
		//Reserved
		UINT64 reserved : 1;                // [3]
		//Execute
		UINT64 execute : 1;                 // [4]
		//Reserved2
		UINT64 reserved2 : 27;              // [5:31]
		//GuestPhysicalAddress
		UINT64 guestPhysicalAddress : 1;    // [32]
		//GuestPageTables
		UINT64 guestPageTables : 1;         // [33]
	} fields;
} NpfExitInfo1;
//LEVEL 4页表
struct PageTableLevel4
{
	typedef PageTableLevel4Entry EntryType;
	PageTableLevel4Entry entries[0x200];
};
//LEVE 1 2 3页表
struct PageTableLevel123
{
	typedef PageTableLevel123Entry EntryType;
	PageTableLevel123Entry entries[0x200];
};

//分配的NPT页表记录条目
struct PageTableRecord
{
	PTR_TYPE pVirtAddr;
	PTR_TYPE pPhyAddr;
	#pragma code_seg()
	PageTableRecord() : pVirtAddr(NULL), pPhyAddr(NULL) {}
	#pragma code_seg()
	PageTableRecord(PTR_TYPE _pVirtAddr,PTR_TYPE _pPhyAddr) : pVirtAddr(_pVirtAddr), pPhyAddr(_pPhyAddr) {}
	#pragma code_seg()
	~PageTableRecord() {}
};

//NPT页表记录器，记录所有封分配初始化的NPT页表的物理地址和虚拟地址
//使用简单的哈希表实现，查询速度会快一些
template<SIZE_TYPE bucketCnt>
class PageTableRecordBacket
{
	KernelVector<PageTableRecord, PT_TAG> data[bucketCnt];

	static SIZE_TYPE GetBucketIdx(PTR_TYPE pa)
	{
		return (pa >> 12) % bucketCnt;
	}

public:
	#pragma code_seg()
	PageTableRecordBacket() {}
	//移动构造和拷贝
	#pragma code_seg()
	PageTableRecordBacket(PageTableRecordBacket&& other)
	{
		*this = static_cast<PageTableRecordBacket&&>(other);
	}
	#pragma code_seg()
	PageTableRecordBacket& operator=(PageTableRecordBacket&& other)
	{
		if (&other != this)
		{
			for (SIZE_TYPE i = 0; i < bucketCnt; i++)
				data[i] = static_cast<KernelVector<PageTableRecord, PT_TAG>&&>(other.data[i]);
		}
		return *this;
	}
	//条目个数
	#pragma code_seg()
	SIZE_TYPE Length() const
	{
		SIZE_TYPE result = 0;
		for (auto& bucket : data)
			result += bucket.Length();
		return result;
	}
	//添加条目
	#pragma code_seg()
	void PushBack(const PageTableRecord& record)
	{
		data[GetBucketIdx(record.pPhyAddr)].PushBack(record);
	}
	//通过物理地址寻找条目索引
	#pragma code_seg()
	PTR_TYPE FindVaFromPa(PTR_TYPE pa) const
	{
		const KernelVector<PageTableRecord, PT_TAG>& bucket = data[GetBucketIdx(pa)];
		for (SIZE_TYPE idx = 0; idx < bucket.Length(); ++idx)
		{
			if (bucket[idx].pPhyAddr == pa)
				return bucket[idx].pVirtAddr;
		}
		return (PTR_TYPE)INVALID_ADDR;
	}
	//删除所有条目
	#pragma code_seg()
	void Clear()
	{
		for (auto& bucket : data)
			bucket.Clear();
	}
	//通过索引获取条目（只读）
	#pragma code_seg()
	const PageTableRecord& operator[](SIZE_TYPE idx) const
	{
		KernelVector<PageTableRecord>* pBucket = NULL;
		SIZE_TYPE cnt = 0;
		for (auto& bucket : data)
		{
			if (cnt > idx)
				break;
			if (idx - cnt < bucket.Length())
			{
				pBucket = &bucket;
				break;
			}
			cnt += bucket.Length();
		}
		if (pBucket == NULL)
		{
			__debugbreak();
			KeBugCheck(MEMORY_MANAGEMENT);
		}
		return (*pBucket)[idx - cnt];
	}
	//通过索引获取条目（读写）
	#pragma code_seg()
	PageTableRecord& operator[](SIZE_TYPE idx)
	{
		KernelVector<PageTableRecord, PT_TAG>* pBucket = NULL;
		SIZE_TYPE cnt = 0;
		for (auto& bucket : data)
		{
			if (cnt > idx)
				break;
			if (idx - cnt < bucket.Length())
			{
				pBucket = &bucket;
				break;
			}
			cnt += bucket.Length();
		}
		if (pBucket == NULL)
		{
			__debugbreak();
			KeBugCheck(MEMORY_MANAGEMENT);
		}
		return (*pBucket)[idx - cnt];
	}
	//通过物理地址删除条目
	#pragma code_seg()
	bool RemoveByPa(PTR_TYPE pa)
	{
		SIZE_TYPE idx = GetBucketIdx(pa);
		KernelVector<PageTableRecord, PT_TAG>& bucket = data[idx];
		for (SIZE_TYPE i = 0; i < bucket.Length(); ++i)
		{
			if (bucket[i].pPhyAddr == pa)
			{
				bucket.Remove(i);
				return true;
			}
		}
		return false;
	}
};

using PageTableRecords3 = PageTableRecordBacket<0x1>;
using PageTableRecords2 = PageTableRecordBacket<0x10>;
using PageTableRecords1 = PageTableRecordBacket<0x40>;

class CoreNptPageTableManager;

//页表管理器
class PageTableManager : public IManager, public INpfInterceptPlugin, public INCr3Provider
{
public:
	//页表条目设置器，这里这样写只是不好写lambda表达式，这个类就类似于一个lambda表达式
	class EntrySetter
	{
		PageTableManager* pageTableManager;
	public:
		#pragma code_seg()
		EntrySetter(PageTableManager* _pageTableManager) : pageTableManager(_pageTableManager) { PAGED_CODE(); NT_ASSERT(pageTableManager != NULL); }
		#pragma code_seg()
		void operator()(PageTableLevel123Entry* pEntry, PTR_TYPE pfn, bool isLargePage, UINT32 level) const {
			{
				//level 为 1 2 3
				NT_ASSERT(level < 4 && level > 0);

				PageTableLevel123Entry permission = {};
				permission.data = pageTableManager->GetDefaultPermission(level);

				permission.fields.present = true;
				permission.fields.size = isLargePage;
				permission.fields.pagePpn = pfn;

				*pEntry = permission;
			}
		}
		#pragma code_seg()
		void operator()(PageTableLevel4Entry* pEntry, PTR_TYPE pfn, bool isLargePage, UINT32 level) const {
			{
				UNREFERENCED_PARAMETER(isLargePage);

				//level 为 4
				NT_ASSERT(level == 4);

				PageTableLevel4Entry permission = {};
				permission.data = pageTableManager->GetDefaultPermission(level);
				permission.fields.ignored2 = 0;

				permission.fields.present = true;
				permission.fields.pagePpn = pfn;

				*pEntry = permission;
			}
		}
	};
private:
	PTR_TYPE pSystemPxe;
	CoreNptPageTableManager* corePageTables;
	SIZE_TYPE pageTableCnt;
	PageTableLevel4Entry defaultPermissionLevel4;
	PageTableLevel123Entry defaultPermissionLevel3;
	PageTableLevel123Entry defaultPermissionLevel2;
	PageTableLevel123Entry defaultPermissionLevel1;
	EntrySetter entrySetter;
public:
	#pragma code_seg("PAGE")
	PageTableManager() : pSystemPxe(NULL), corePageTables(NULL), pageTableCnt(0), entrySetter(this)
	{
		PAGED_CODE();

		//设置默认权限
		defaultPermissionLevel3 = {};
		defaultPermissionLevel3.fields.writeable = true;
		defaultPermissionLevel3.fields.userAccess = true;

		defaultPermissionLevel1 = defaultPermissionLevel2 = defaultPermissionLevel3;

		defaultPermissionLevel4 = {};
		defaultPermissionLevel4.fields.writeable = true;
		defaultPermissionLevel4.fields.userAccess = true;
	}
	void SetDefaultPermission(UINT64 permission, UINT32 level);
	UINT64 GetDefaultPermission(UINT32 level) const;
	virtual bool HandleNpf(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters,
		PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr) override;
	virtual PVOID GetNCr3ForCore(UINT32 cpuIdx) override;
	virtual NTSTATUS Init() override;
	virtual void Deinit() override;
	#pragma code_seg()
	CoreNptPageTableManager* GetCoreNptPageTables() { return corePageTables; }
	SIZE_TYPE GetCoreNptPageTablesCnt() const { return pageTableCnt; }
	#pragma code_seg("PAGE")
	virtual ~PageTableManager() { PAGED_CODE(); Deinit(); }
};

//每个核心的NPT页表管理器
class CoreNptPageTableManager
{
	//顶层页表的物理地址
	PTR_TYPE pNptPageTableVa;
	//顶层页表的虚拟地址
	PTR_TYPE pNptPageTablePa;
	PageTableRecords3 level3Records;
	PageTableRecords2 level2Records;
	PageTableRecords1 level1Records;
	PageTableManager::EntrySetter* pEntrySetter;

public:
	//删除默认构造
	CoreNptPageTableManager() = delete;
	//使用PageTableManager::EntrySetter构造，PageTableManager::EntrySetter决定构造页表的默认权限
	CoreNptPageTableManager(PageTableManager::EntrySetter* _pEntrySetter) : pNptPageTableVa(INVALID_ADDR), pEntrySetter(_pEntrySetter), pNptPageTablePa(INVALID_ADDR) {}
	//移动构造
	#pragma code_seg()
	CoreNptPageTableManager(CoreNptPageTableManager&& other)
	{
		*this = static_cast<CoreNptPageTableManager&&>(other);
	}
	//移动构造
	#pragma	code_seg()
	CoreNptPageTableManager& operator=(CoreNptPageTableManager&& other)
	{
		if (&other != this)
		{
			pEntrySetter = other.pEntrySetter;
			pNptPageTableVa = other.pNptPageTableVa;
			level3Records = static_cast<PageTableRecords3&&>(other.level3Records);
			level2Records = static_cast<PageTableRecords2&&>(other.level2Records);
			level1Records = static_cast<PageTableRecords1&&>(other.level1Records);
			other.pNptPageTableVa = INVALID_ADDR;
		}
		return *this;
	}
	#pragma code_seg("PAGE")
	~CoreNptPageTableManager() { PAGED_CODE(); Deinit(); }
	//映射缺页函数
	NTSTATUS FixPageFault(PTR_TYPE startAddr, PTR_TYPE endAddr, bool usingLargePage);
	//isUsing 为 false 代表还原大页
	NTSTATUS UsingSmallPage(PTR_TYPE phyAddr, bool isUsing);
	//小页映射函数，等同于FixPageFault(begPhyAddr, endPhyAddr, false)
	NTSTATUS MapSmallPageByPhyAddr(PTR_TYPE begPhyAddr, PTR_TYPE endPhyAddr);
	//交换小页的最终物理地址
	NTSTATUS SwapSmallPagePpn(PTR_TYPE phyAddr1, PTR_TYPE phyAddr2, UINT32 level);
	//获取指定虚拟地址对应的NPT页表的最终PPN对应的物理地址
	NTSTATUS GetNptFinalAddrForPhyAddr(PTR_TYPE phyAddr, PTR_TYPE& pNptFinalAddr, PTR_TYPE& level);
	//修改所有最底层页表的权限
	void ChangeAllEndLevelPageTablePermession(PageTableLevel123Entry entry);
	//修改特定页表的值
	NTSTATUS ChangePageTableEntryPermession(PTR_TYPE pa, PageTableLevel123Entry entry, UINT32 level);
	void Deinit();
	NTSTATUS BuildNptPageTable();
	#pragma code_seg()
	PTR_TYPE GetNptPageTableVa() const { return pNptPageTableVa; }
	PTR_TYPE GetNptPageTablePa() const { return pNptPageTablePa; }
	//通过物理地址也页表级速寻找页表项虚拟地址
	PVOID FindPageTableByPhyAddr(PTR_TYPE pa, UINT32 level) const;
};

//查询当前CR3（顶层页表）的虚拟地址
void GetSysPXEVirtAddr(PTR_TYPE* pPxeOut, PTR_TYPE pxePhyAddr);

#endif
