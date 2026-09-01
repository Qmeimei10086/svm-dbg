#include "PageTable.h"
#include <intrin.h>

//计算物理地址的PFN
#define GET_PFN_FROM_PHYADDR(phyAddr) (((phyAddr) >> 12) & 0xFFFFFFFFF)
//PFN转换为物理地址
#define GET_PHYADDR_FROM_PFN(pfn) (((pfn) & 0xFFFFFFFFF) << 12)
//简单移位
#define MUL_UNIT(value, rightShift) ((value) << (rightShift))

//获取当前页表基地址（虚拟地址）
//适用于Windows 10 1607 之后的页表随机化
#pragma code_seg()
void GetSysPXEVirtAddr(PTR_TYPE* pPxeOut, PTR_TYPE pxePhyAddr)
{
	//读取Cr3物理地址并使用Windows内核函数转换为虚拟地址
	//注意：MmGetVirtualForPhysical被微软标记为保留，除了名字外啥也没提
	pxePhyAddr &= 0xFFFFFFFFFFFFF000;

	PTR_TYPE testAddr = NULL;
	PTR_TYPE testPhyAddr = NULL;
	BOOLEAN matchedPxe = FALSE;

	//通过测试内存页面和物理地址确认自映射页表编号
	PTR_TYPE index = 0;
	for (index = 0; index < 0x200; index++)
	{
		//构造可能的pxe地址
		if (index < 0x100)
			testAddr = 0x0000000000000000;
		else
			testAddr = 0xFFFF000000000000;

		testAddr |= index << 39 | index << 30 | index << 21 | index << 12;

		if (testAddr == NULL) continue;

		//确认可以读
		if (MmIsAddressValid((PVOID)testAddr))
		{
			// MmIsAddressValid 只对非分页 但是 页表 一定是 非分页内存 直接通过范围确认在Pxe表内
			testPhyAddr = MmGetPhysicalAddress((PVOID)testAddr).QuadPart;

			if (testPhyAddr == pxePhyAddr)
			{
				matchedPxe = TRUE;
				break;
			}
		}
	}

	if (!matchedPxe)
	{
		*pPxeOut = NULL;
		return;
	}

	*pPxeOut = testAddr;

	return;
}

//分配新的子页表并和当前页表项关联
#pragma code_seg()
template<typename EntryType, typename TableType, typename EntrySetter, typename PageTableRecords, PTR_TYPE level>
NTSTATUS AllocNewPageTable(EntryType* fatherEntry, PageTableRecords& records, PTR_TYPE& va, EntrySetter entrySetter)
{
	NTSTATUS status = STATUS_SUCCESS;

	do
	{
		//分配内存
		TableType* pSubTable = (TableType*)AllocNonPagedMem(sizeof * pSubTable, PT_TAG);
		if (pSubTable == NULL)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		//返回新页表虚拟地址
		va = (PTR_TYPE)pSubTable;
		//获取新页表物理地址
		PTR_TYPE pa = (PTR_TYPE)MmGetPhysicalAddress((PVOID)pSubTable).QuadPart;
		//添加到页表记录
		records.PushBack(PageTableRecord((PTR_TYPE)pSubTable, pa));
		//设置父页表项
		entrySetter(fatherEntry, GET_PFN_FROM_PHYADDR(pa), false, level);
		//清空子页表
		RtlZeroMemory(pSubTable, sizeof * pSubTable);

	} while (false);

	return status;
}
//*************************************页表构建函数开始*************************************
//禁用对C++17 if constexpr使用的警告
#pragma warning(disable : 4984)

//高级别页表构建
#pragma code_seg()
template<typename TableType, typename SubTableType, PTR_TYPE level, bool checkLargePage, typename NextStep, NextStep nextStep, typename EntrySetter>
NTSTATUS ProcessNptPageTableFrontLevelImpl(TableType* pTable, PTR_TYPE startPhyAddr, PTR_TYPE endPhyAddr, PageTableRecords3& level3Records, PageTableRecords2& level2Records, PageTableRecords1& level1Records, EntrySetter entrySetter)
{
	NTSTATUS status = STATUS_SUCCESS;

	do
	{
		//检查参数，结束地址必须大于起始地址
		if (endPhyAddr <= startPhyAddr)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		constexpr PTR_TYPE rightShift = (level - 1) * 9 + 12;
		constexpr PTR_TYPE unitMask1 = (static_cast<PTR_TYPE>(-1) << rightShift);
		constexpr PTR_TYPE unitMask2 = ~unitMask1;
		PTR_TYPE startBase = startPhyAddr & unitMask1;
		PTR_TYPE startIdx = ((startPhyAddr >> rightShift) & 0x1ff);
		PTR_TYPE idxCnt = ((endPhyAddr - startBase) & unitMask1) >> rightShift;
		if ((endPhyAddr - startBase) & unitMask2)
			++idxCnt;
		PTR_TYPE endIdx = startIdx + idxCnt;

		//检查参数，防止越界
		if (endIdx > 0x200)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		//初始化页表项
		for (PTR_TYPE idx = startIdx; idx < endIdx; ++idx)
		{
			PTR_TYPE va = (PTR_TYPE)INVALID_ADDR;
			//如果该页表项为空，则分配子页表并初始化，并获取子页表的虚拟地址
			if (!pTable->entries[idx].fields.present)
			{
				if constexpr (level == 2)
					status = AllocNewPageTable<typename TableType::EntryType, SubTableType, EntrySetter, PageTableRecords1, level>(&pTable->entries[idx], level1Records, va, entrySetter);
				else if constexpr (level == 3)
					status = AllocNewPageTable<typename TableType::EntryType, SubTableType, EntrySetter, PageTableRecords2, level>(&pTable->entries[idx], level2Records, va, entrySetter);
				else
					status = AllocNewPageTable<typename TableType::EntryType, SubTableType, EntrySetter, PageTableRecords3, level>(&pTable->entries[idx], level3Records, va, entrySetter);
				if (!NT_SUCCESS(status))
					break;
			}
			//如果该页表项不为空，直接获取子页表虚拟地址
			else
			{
				//不处理大页
				if constexpr (checkLargePage)
				{
					if (pTable->entries[idx].fields.size)
						return STATUS_SUCCESS;
				}

				//新建页表时记录了虚拟地址和物理地址
				//在记录项里面通过物理地址查找虚拟地址
				//避免了微软保留API（MmGetVirtualForPhysical）的使用

				if constexpr (level == 2)
					va = level1Records.FindVaFromPa(GET_PHYADDR_FROM_PFN(pTable->entries[idx].fields.pagePpn));
				else if constexpr (level == 3)
					va = level2Records.FindVaFromPa(GET_PHYADDR_FROM_PFN(pTable->entries[idx].fields.pagePpn));
				else
					va = level3Records.FindVaFromPa(GET_PHYADDR_FROM_PFN(pTable->entries[idx].fields.pagePpn));

				if (va == INVALID_ADDR)
				{
					status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}
			}

			PTR_TYPE newStartPhyAddr = startBase + MUL_UNIT(idx - startIdx, rightShift);
			PTR_TYPE newEndPhyAddr = newStartPhyAddr + MUL_UNIT(static_cast<PTR_TYPE>(1), rightShift);

			if (startPhyAddr > newStartPhyAddr)
				newStartPhyAddr = startPhyAddr;

			if (endPhyAddr < newEndPhyAddr)
				newEndPhyAddr = endPhyAddr;

			//调用下一步处理函数处理子页表
			status = nextStep((SubTableType*)va, newStartPhyAddr, newEndPhyAddr, level3Records, level2Records, level1Records, entrySetter);
			if (!NT_SUCCESS(status))
				break;
		}

		if (!NT_SUCCESS(status))
			break;

	} while (false);

	return status;
}


//（最低级别页表）的数据填充
#pragma code_seg()
template<PTR_TYPE level, bool isLargePage, typename EntrySetter>
NTSTATUS ProcessNptPageTableeEndLevelImpl(PageTableLevel123* pTable, PTR_TYPE startPhyAddr, PTR_TYPE endPhyAddr, PageTableRecords3& level3Records, PageTableRecords2 & level2Records, PageTableRecords1& level1Records, EntrySetter entrySetter)
{
	UNREFERENCED_PARAMETER(level1Records);
	UNREFERENCED_PARAMETER(level2Records);
	UNREFERENCED_PARAMETER(level3Records);

	constexpr PTR_TYPE rightShift = (level - 1) * 9 + 12;
	constexpr PTR_TYPE unitMask1 = (static_cast<PTR_TYPE>(-1) << rightShift);
	constexpr PTR_TYPE unitMask2 = ~unitMask1;

	NTSTATUS status = STATUS_SUCCESS;

	do
	{
		//检查参数，结束地址必须大于起始地址
		if (endPhyAddr <= startPhyAddr)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		PTR_TYPE startBase = startPhyAddr & unitMask1;
		PTR_TYPE startIdx = ((startPhyAddr >> rightShift) & 0x1ff);
		PTR_TYPE idxCnt = ((endPhyAddr - startBase) & unitMask1) >> rightShift;
		if ((endPhyAddr - startBase) & unitMask2)
			++idxCnt;
		PTR_TYPE endIdx = startIdx + idxCnt;

		//检查参数，防止越界
		if (endIdx > 0x200)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		//处理每个页表项
		for (PTR_TYPE idx = startIdx; idx < endIdx; ++idx)
		{
			if (!pTable->entries[idx].fields.present)
				entrySetter(&pTable->entries[idx], GET_PFN_FROM_PHYADDR(startBase + MUL_UNIT(idx - startIdx, rightShift)), isLargePage, level);
		}
	} while (false);

	return status;
}

//*************************************页表构建函数结束*************************************

using NPT_Level123_Processor = NTSTATUS(*)(PageTableLevel123*, PTR_TYPE, PTR_TYPE, PageTableRecords3&, PageTableRecords2&, PageTableRecords1&, PageTableManager::EntrySetter&);
using NPT_Level4_Processor = NTSTATUS(*)(PageTableLevel4*, PTR_TYPE, PTR_TYPE, PageTableRecords3&, PageTableRecords2&, PageTableRecords1&, PageTableManager::EntrySetter&);

//调用小页面处理函数
#pragma code_seg()
static NTSTATUS CallNptSmallPageProcessor(PageTableLevel4* pTable, PTR_TYPE startPhyAddr, PTR_TYPE endPhyAddr, PageTableRecords3& level3Records, PageTableRecords2& level2Records, PageTableRecords1& level1Records, PageTableManager::EntrySetter& entrySetter)
{
	//嵌套模板函数，用于补全小页面的缺失
	constexpr NPT_Level123_Processor level1NptProcessor = ProcessNptPageTableeEndLevelImpl<1, false, PageTableManager::EntrySetter&>;
	constexpr NPT_Level123_Processor level2NptProcessor = ProcessNptPageTableFrontLevelImpl<PageTableLevel123, PageTableLevel123, 2, true, NPT_Level123_Processor, level1NptProcessor, PageTableManager::EntrySetter&>;
	constexpr NPT_Level123_Processor level3NptProcessor = ProcessNptPageTableFrontLevelImpl<PageTableLevel123, PageTableLevel123, 3, false, NPT_Level123_Processor, level2NptProcessor, PageTableManager::EntrySetter&>;
	constexpr NPT_Level4_Processor   level4NptProcessor = ProcessNptPageTableFrontLevelImpl<PageTableLevel4, PageTableLevel123, 4, false, NPT_Level123_Processor, level3NptProcessor, PageTableManager::EntrySetter&>;
	return level4NptProcessor(pTable, startPhyAddr, endPhyAddr, level3Records, level2Records, level1Records, entrySetter);
}

using NPT_Large_Level23_Processor = NTSTATUS(*)(PageTableLevel123*, PTR_TYPE, PTR_TYPE, PageTableRecords3&, PageTableRecords2&, PageTableRecords1&, PageTableManager::EntrySetter&);
using NPT_Large_Level4_Processor = NTSTATUS(*)(PageTableLevel4*, PTR_TYPE, PTR_TYPE, PageTableRecords3&, PageTableRecords2&, PageTableRecords1&, PageTableManager::EntrySetter&);

//调用大页面构建函数指针
#pragma code_seg()
static NTSTATUS CallNptLargePageProcessor(PageTableLevel4* pTable, PTR_TYPE startPhyAddr, PTR_TYPE endPhyAddr, PageTableRecords3& level3Records, PageTableRecords2& level2Records, PageTableRecords1& level1Records, PageTableManager::EntrySetter& entrySetter)
{
	constexpr NPT_Large_Level23_Processor level2NptProcessor = ProcessNptPageTableeEndLevelImpl<2, true, PageTableManager::EntrySetter&>;
	constexpr NPT_Large_Level23_Processor level3NptProcessor = ProcessNptPageTableFrontLevelImpl<PageTableLevel123, PageTableLevel123, 3, false, NPT_Large_Level23_Processor, level2NptProcessor, PageTableManager::EntrySetter&>;
	constexpr NPT_Large_Level4_Processor  level4NptProcessor = ProcessNptPageTableFrontLevelImpl<PageTableLevel4, PageTableLevel123, 4, false, NPT_Large_Level23_Processor, level3NptProcessor, PageTableManager::EntrySetter&>;
	return level4NptProcessor(pTable, startPhyAddr, endPhyAddr, level3Records, level2Records, level1Records, entrySetter);
}

#pragma code_seg()
void PageTableManager::SetDefaultPermission(UINT64 permission, UINT32 level)
{
	switch (level)
	{
	case 4:
		defaultPermissionLevel4.data = permission;
		break;
	case 3:
		defaultPermissionLevel3.data = permission;
		break;
	case 2:
		defaultPermissionLevel2.data = permission;
		break;
	case 1:
		defaultPermissionLevel1.data = permission;
		break;
	default:
		break;
	}
}

#pragma code_seg()
UINT64 PageTableManager::GetDefaultPermission(UINT32 level) const
{
	switch (level)
	{
	case 4:
		return defaultPermissionLevel4.data;
	case 3:
		return defaultPermissionLevel3.data;
	case 2:
		return defaultPermissionLevel2.data;
	case 1:
		return defaultPermissionLevel1.data;
	default:
		__debugbreak();
		KeBugCheck(MANUALLY_INITIATED_CRASH);
		break;
	}
}

//处理NPF
#pragma code_seg()
bool PageTableManager::HandleNpf(VirtCpuInfo* pVirtCpuInfo, GenericRegisters* pGuestRegisters, PVOID pGuestVmcbPhyAddr, PVOID pHostVmcbPhyAddr)
{
	UNREFERENCED_PARAMETER(pGuestRegisters);
	UNREFERENCED_PARAMETER(pGuestVmcbPhyAddr);
	UNREFERENCED_PARAMETER(pHostVmcbPhyAddr);

	bool result = false;

	NpfExitInfo1 exitInfo = {};
	PTR_TYPE pa = pVirtCpuInfo->guestVmcb.controlFields.exitInfo2;
	exitInfo.data = pVirtCpuInfo->guestVmcb.controlFields.exitInfo1;

	if (!exitInfo.fields.present)
	{
		PTR_TYPE paStart = pa - pa % PAGE_SIZE;
		PTR_TYPE paEnd = paStart + PAGE_SIZE;

		//逐级生成页表，如果失败则蓝屏
		if (!NT_SUCCESS(corePageTables[pVirtCpuInfo->otherInfo.cpuIdx].FixPageFault(paStart, paEnd, true)))
		{
			__debugbreak();
			KeBugCheck(MANUALLY_INITIATED_CRASH);
		}

		result = true;
	}

	return result;
}

#pragma code_seg("PAGE")
PVOID PageTableManager::GetNCr3ForCore(UINT32 cpuIdx)
{
	PAGED_CODE();
	const CoreNptPageTableManager* pageTables = GetCoreNptPageTables();
	SIZE_TYPE cnt = GetCoreNptPageTablesCnt();
	if (cpuIdx >= cnt)
		return (PVOID)INVALID_ADDR;
	else
		return (PVOID)MmGetPhysicalAddress((PVOID)pageTables[cpuIdx].GetNptPageTableVa()).QuadPart;
}

///通过物理地址和需要的级数查找页表，失败返回INVALID_ADDR
#pragma code_seg()
PVOID CoreNptPageTableManager::FindPageTableByPhyAddr(PTR_TYPE guestPa, UINT32 level) const
{
	PVOID result = (PVOID)INVALID_ADDR;
	PTR_TYPE tempPa = INVALID_ADDR;
	PTR_TYPE tempVa = INVALID_ADDR;
	PTR_TYPE pageTableIdx = ((guestPa >> 39) & 0x1ff);
	SIZE_T levelIdx = 0;

	do
	{
		if (level >= 4)
		{
			result = (PVOID)pNptPageTableVa;
			break;
		}

		if (!((PageTableLevel4*)pNptPageTableVa)->entries[pageTableIdx].fields.present)
			break;

		tempPa = GET_PHYADDR_FROM_PFN(((PageTableLevel4*)pNptPageTableVa)->entries[pageTableIdx].fields.pagePpn);
		tempVa = level3Records.FindVaFromPa(tempPa);

		if (tempVa == INVALID_ADDR)
			break;
		else
			result = (PVOID)tempVa;

		for (levelIdx = 2; levelIdx >= level; --levelIdx)
		{
			pageTableIdx = ((guestPa >> (levelIdx * 9 + 12)) & 0x1ff);

			if (!((PageTableLevel123*)tempVa)->entries[pageTableIdx].fields.present)
				break;

			if (((PageTableLevel123*)tempVa)->entries[pageTableIdx].fields.size && levelIdx != level)
				break;

			tempPa = GET_PHYADDR_FROM_PFN(((PageTableLevel123*)tempVa)->entries[pageTableIdx].fields.pagePpn);
			if (levelIdx == 1)
				tempVa = level1Records.FindVaFromPa(tempPa);
			else if (levelIdx == 2)
				tempVa = level2Records.FindVaFromPa(tempPa);
			else
				tempVa = level3Records.FindVaFromPa(tempPa);

			if (tempVa == INVALID_ADDR)
				break;

			result = (PVOID)tempVa;
		}

		if (levelIdx >= level)
			result = (PVOID)INVALID_ADDR;

	} while (false);

	return result;
}

#pragma code_seg()
NTSTATUS CoreNptPageTableManager::FixPageFault(PTR_TYPE startAddr, PTR_TYPE endAddr, bool usingLargePage)
{
	if (usingLargePage)
		return CallNptLargePageProcessor((PageTableLevel4*)pNptPageTableVa, startAddr, endAddr, level3Records, level2Records, level1Records, *pEntrySetter);
	else
		return CallNptSmallPageProcessor((PageTableLevel4*)pNptPageTableVa, startAddr, endAddr, level3Records, level2Records, level1Records, *pEntrySetter);
}

#pragma code_seg()
NTSTATUS CoreNptPageTableManager::UsingSmallPage(PTR_TYPE phyAddr, bool isUsing)
{
	NTSTATUS status = STATUS_SUCCESS;
	PageTableLevel123* pTargetPageTable = (PageTableLevel123*)INVALID_ADDR;
	PTR_TYPE pageTableIdx = ((phyAddr >> 21) & 0x1ff);

	do
	{
		//物理地址的LEVEL4偏移必须是0
		if ((phyAddr >> 12) & 0x1ff)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		//查找目标页表
		pTargetPageTable = (PageTableLevel123*)FindPageTableByPhyAddr(phyAddr, 2);
		//找不到目标页表返回失败
		if (pTargetPageTable == (PageTableLevel123*)INVALID_ADDR)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		//改小页
		if (isUsing)
		{
			//如果已经是小页，就不用再修改属性
			if (pTargetPageTable->entries[pageTableIdx].fields.present &&
				!pTargetPageTable->entries[pageTableIdx].fields.size)
				return STATUS_SUCCESS;

			//更改属性值到未映射状态
			pTargetPageTable->entries[pageTableIdx].fields.present = false;
			pTargetPageTable->entries[pageTableIdx].fields.size = false;
		}
		//改大页
		else
		{
			//如果已经是大页，就不用再修改属性
			if (pTargetPageTable->entries[pageTableIdx].fields.present &&
				pTargetPageTable->entries[pageTableIdx].fields.size)
				return STATUS_SUCCESS;

			PTR_TYPE finalPa = GET_PHYADDR_FROM_PFN(pTargetPageTable->entries[pageTableIdx].fields.pagePpn);
			PTR_TYPE finalVa = level1Records.FindVaFromPa(finalPa);

			//删除Leve4页面
			if (finalVa != INVALID_ADDR)
			{
				level1Records.RemoveByPa(finalPa);
				FreeNonPagedMem((PVOID)finalVa, PT_TAG);
			}

			//还原属性值
			(*pEntrySetter)(&pTargetPageTable->entries[pageTableIdx], GET_PFN_FROM_PHYADDR(phyAddr), true, 2);
		}

	} while (false);

	return status;
}

#pragma code_seg()
NTSTATUS CoreNptPageTableManager::MapSmallPageByPhyAddr(PTR_TYPE begPhyAddr, PTR_TYPE endPhyAddr)
{
	return FixPageFault(begPhyAddr, endPhyAddr, false);
}

#pragma code_seg()
NTSTATUS CoreNptPageTableManager::SwapSmallPagePpn(PTR_TYPE phyAddr1, PTR_TYPE phyAddr2, UINT32 level)
{
	PTR_TYPE pageTableIdx1 = ((phyAddr1 >> 12) & 0x1ff);
	PTR_TYPE pageTableIdx2 = ((phyAddr2 >> 12) & 0x1ff);
	PageTableLevel123* pageTable1 = (PageTableLevel123*)FindPageTableByPhyAddr(phyAddr1, level);
	PageTableLevel123* pageTable2 = (PageTableLevel123*)FindPageTableByPhyAddr(phyAddr2, level);
	PageTableLevel123Entry swapEntry = {};

	if (pageTable1 == (PageTableLevel123*)INVALID_ADDR || pageTable2 == (PageTableLevel123*)INVALID_ADDR)
		return STATUS_INVALID_PARAMETER;

	swapEntry = pageTable1->entries[pageTableIdx1];
	pageTable1->entries[pageTableIdx1].fields.pagePpn = pageTable2->entries[pageTableIdx2].fields.pagePpn;
	pageTable2->entries[pageTableIdx2].fields.pagePpn = swapEntry.fields.pagePpn;

	return STATUS_SUCCESS;
}

#pragma code_seg()
NTSTATUS CoreNptPageTableManager::GetNptFinalAddrForPhyAddr(PTR_TYPE phyAddr, PTR_TYPE& pNptFinalAddr, PTR_TYPE& level)
{
	PageTableLevel123* pageTable1 = (PageTableLevel123*)FindPageTableByPhyAddr(phyAddr, 2);
	PageTableLevel123* pageTable2 = (PageTableLevel123*)FindPageTableByPhyAddr(phyAddr, 1);

	if (pageTable1 == (PageTableLevel123*)INVALID_ADDR && pageTable2 == (PageTableLevel123*)INVALID_ADDR)
		return STATUS_UNSUCCESSFUL;

	if (pageTable2 != (PageTableLevel123*)INVALID_ADDR)
	{
		pNptFinalAddr = GET_PHYADDR_FROM_PFN(pageTable2->entries[(phyAddr >> 12) & 0x1ff].fields.pagePpn);
		level = 1;
	}
	else
	{
		pNptFinalAddr = GET_PHYADDR_FROM_PFN(pageTable1->entries[(phyAddr >> 21) & 0x1ff].fields.pagePpn);
		level = 2;
	}

	return STATUS_SUCCESS;
}

#pragma code_seg()
void ChangeAllEndLevelPageTablePremessionSub(PageTableLevel123* pPageTable, UINT32 level, PageTableRecords3& level3Records, PageTableRecords2& level2Records, PageTableRecords1& level1Records, PageTableLevel123Entry entry)
{
	for (SIZE_TYPE idx = 0; idx < GetArrayElementCnt(pPageTable->entries); ++idx)
	{
		if (level == 1 || pPageTable->entries[idx].fields.size)
		{
			//写入权限
			entry.fields.pagePpn = pPageTable->entries[idx].fields.pagePpn;
			entry.fields.size = pPageTable->entries[idx].fields.size;

			pPageTable->entries[idx] = entry;
		}
		else
		{
			PageTableLevel123* pSubPageTable = NULL;

			if (level == 2)
				pSubPageTable = (PageTableLevel123*)level1Records.FindVaFromPa(GET_PHYADDR_FROM_PFN(pPageTable->entries[idx].fields.pagePpn));
			else if (level == 3)
				pSubPageTable = (PageTableLevel123*)level2Records.FindVaFromPa(GET_PHYADDR_FROM_PFN(pPageTable->entries[idx].fields.pagePpn));
			else
				pSubPageTable = (PageTableLevel123*)level3Records.FindVaFromPa(GET_PHYADDR_FROM_PFN(pPageTable->entries[idx].fields.pagePpn));

			if (pSubPageTable != (PageTableLevel123*)INVALID_ADDR)
				ChangeAllEndLevelPageTablePremessionSub(pSubPageTable, level - 1, level3Records, level2Records, level1Records, entry);
		}
	}
}

#pragma code_seg()
void CoreNptPageTableManager::ChangeAllEndLevelPageTablePermession(PageTableLevel123Entry entry)
{
	entry.fields.present = true;

	PageTableLevel4* pPageTable = (PageTableLevel4*)pNptPageTableVa;

	for (SIZE_TYPE idx = 0; idx < GetArrayElementCnt(pPageTable->entries); ++idx)
	{
		PageTableLevel123* pSubPageTable = (PageTableLevel123*)level3Records.FindVaFromPa(GET_PHYADDR_FROM_PFN(pPageTable->entries[idx].fields.pagePpn));
		if (pSubPageTable != (PageTableLevel123*)INVALID_ADDR)
			ChangeAllEndLevelPageTablePremessionSub(pSubPageTable, 3, level3Records, level2Records, level1Records, entry);
	}
}

#pragma code_seg()
NTSTATUS CoreNptPageTableManager::ChangePageTableEntryPermession(PTR_TYPE guestPa, PageTableLevel123Entry entry, UINT32 level)
{
	//找到对应的页表
	PageTableLevel123* pageTable = (PageTableLevel123*)FindPageTableByPhyAddr(guestPa, level);
	PageTableLevel123Entry* pTargetEntry = NULL;

	if (pageTable == (PageTableLevel123*)INVALID_ADDR)
		return STATUS_UNSUCCESSFUL;

	//找到对应的页表项
	pTargetEntry = &pageTable->entries[(guestPa >> (12 + (level - 1) * 9)) & 0x1ff];

	//先构造修改后的页表项的数据，再整体拷贝到页表项中，这样会快一些
	entry.fields.pagePpn = pTargetEntry->fields.pagePpn;
	entry.fields.present = pTargetEntry->fields.present;
	entry.fields.size = pTargetEntry->fields.size;

	*pTargetEntry = entry;

	return STATUS_SUCCESS;
}

#pragma code_seg("PAGE")
void CoreNptPageTableManager::Deinit()
{
	PAGED_CODE();
	if (pNptPageTableVa != INVALID_ADDR)
	{
		//LEVEL 3 页表释放
		for (SIZE_TYPE idx = 0; idx < level3Records.Length(); ++idx)
			FreeNonPagedMem((PVOID)level3Records[idx].pVirtAddr, PT_TAG);

		level3Records.Clear();

		//LEVEL 2 页表释放
		for (SIZE_TYPE idx = 0; idx < level2Records.Length(); ++idx)
			FreeNonPagedMem((PVOID)level2Records[idx].pVirtAddr, PT_TAG);

		level2Records.Clear();

		//LEVEL 1 页表释放
		for (SIZE_TYPE idx = 0; idx < level1Records.Length(); ++idx)
			FreeNonPagedMem((PVOID)level1Records[idx].pVirtAddr, PT_TAG);

		level1Records.Clear();

		//LEVEL 4 页表释放
		FreeNonPagedMem((PVOID)pNptPageTableVa, PT_TAG);

		//置空
		pNptPageTableVa = INVALID_ADDR;
		pNptPageTablePa = INVALID_ADDR;
	}
}

#pragma code_seg("PAGE")
NTSTATUS CoreNptPageTableManager::BuildNptPageTable()
{
	PAGED_CODE();
	NTSTATUS status = STATUS_SUCCESS;

	do
	{
		//分配的内存刚好是一个页面，使用ExAllocatePool2函数比使用MmAllocateContiguousMemory快很多，其他地方也是类似
		PageTableLevel4* pNptLevel4PageTable = (PageTableLevel4*)AllocNonPagedMem(sizeof * pNptLevel4PageTable, PT_TAG);
		if (pNptLevel4PageTable == NULL)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		pNptPageTableVa = (PTR_TYPE)pNptLevel4PageTable;

		RtlZeroMemory(pNptLevel4PageTable, sizeof(*pNptLevel4PageTable));

		//构建页表
		//初始化时全部使用2MB大页，节约内存同时可以覆盖全部物理地址
		//需要HOOK时把对应部分改成小页即可
		status = CallNptLargePageProcessor(pNptLevel4PageTable, 0x0, 0x000000FFFFFFFFFF, level3Records, level2Records, level1Records, *pEntrySetter);

		//获取顶层页表的物理地址
		pNptPageTablePa = MmGetPhysicalAddress((PVOID)pNptPageTableVa).QuadPart;

	} while (false);

	return status;
}

#pragma code_seg("PAGE")
NTSTATUS PageTableManager::Init()
{
	PAGED_CODE();
	NTSTATUS status = STATUS_SUCCESS;

	do
	{
		if (corePageTables == NULL)
		{
			//获取当前核心数量， 分配 sizeof(CoreNptPageTableManager) * 核心数 内存
			UINT32 cpuCnt = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
			corePageTables = (CoreNptPageTableManager*)AllocNonPagedMem(sizeof * corePageTables * cpuCnt, PT_TAG);
			if (corePageTables == NULL)
			{
				KdPrint(("PageTableManager::Init(): Can not allocate memory for core pagetable manager.\n"));
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}
			//页表数量等同于核心数
			pageTableCnt = cpuCnt;
			//初始化CoreNptPageTableManager并构建页表
			for (SIZE_TYPE idx = 0; idx < pageTableCnt; ++idx)
			{
				CallConstructor(&corePageTables[idx], &entrySetter);
				status = corePageTables[idx].BuildNptPageTable();
				if (!NT_SUCCESS(status))
				{
					KdPrint(("PageTableManager::Init(): Can not build npt page table.\n"));
					break;
				}
			}
		}

	} while (false);

	if (!NT_SUCCESS(status))
		Deinit();

	return status;
}

#pragma code_seg("PAGE")
void PageTableManager::Deinit()
{
	PAGED_CODE();
	if (corePageTables != NULL)
	{
		//释放CoreNptPageTableManager占用的资源
		for (SIZE_TYPE idx = 0; idx < pageTableCnt; ++idx)
			CallDestroyer(&corePageTables[idx]);
		//释放CoreNptPageTableManager本省占用的内存
		FreeNonPagedMem(corePageTables, PT_TAG);
		//置空成员
		corePageTables = NULL;
		pageTableCnt = 0;
	}
}