#ifndef UNPUBLIC_API_H
#define UNPUBLIC_API_H

#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>

extern "C" NTSYSAPI NTSTATUS ZwReadVirtualMemory(
	HANDLE ProcessHandle,
	PVOID BaseAddress,
	PVOID Buffer,
	SIZE_T BufferSize,
	PSIZE_T BytesRead
);


#endif
