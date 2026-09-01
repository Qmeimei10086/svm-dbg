#include "DbgHook/dbg.h"
#include "NPT-Hook/NptHook.h"

SYMBOLS_DATA g_SymbolsData = { 0 };

#define CTL_LOAD_DRIVER 0x800
#define IOCTL_LOAD_SYMBOLS CTL_CODE(FILE_DEVICE_UNKNOWN, CTL_LOAD_DRIVER, METHOD_BUFFERED, FILE_ANY_ACCESS)

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UnHookFuncs();

	if (DriverObject->DeviceObject)
	{
		UNICODE_STRING DosDeviceName;
		RtlInitUnicodeString(&DosDeviceName, L"\\DosDevices\\YCData");
		IoDeleteSymbolicLink(&DosDeviceName);
		IoDeleteDevice(DriverObject->DeviceObject);
	}
}

NTSTATUS DrvComm(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS DrvIOCTLDispatcher(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS Status = STATUS_SUCCESS;

	switch (Stack->Parameters.DeviceIoControl.IoControlCode)
	{
		case IOCTL_LOAD_SYMBOLS:
		{
			__try
			{
				if (Stack->Parameters.DeviceIoControl.InputBufferLength >= SYMBOLS_DATA_BASE_SIZE)
				{
					memcpy(&g_SymbolsData, Irp->AssociatedIrp.SystemBuffer, SYMBOLS_DATA_BASE_SIZE);
					// Old (r3) clients do not send Flags; default to 0.
					g_SymbolsData.Flags = 0;
					if (Stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(SYMBOLS_DATA))
						g_SymbolsData.Flags = ((PSYMBOLS_DATA)Irp->AssociatedIrp.SystemBuffer)->Flags;

					if (!DbgInit(g_SymbolsData.Flags))
						Status = STATUS_UNSUCCESSFUL;
				}
				else
				{
					Status = STATUS_BUFFER_TOO_SMALL;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Status = GetExceptionCode();
			}
			break;
		}
		default:
			Status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	Irp->IoStatus.Status = Status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Status;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT Driver, PCUNICODE_STRING Reg)
{
	UNREFERENCED_PARAMETER(Reg);
	Driver->DriverUnload = DriverUnload;

	// Start the AMD-V hypervisor + NPT hook engine.
	NTSTATUS status = NptHookInitialize();
	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(77, 0, "[DbgHook] NptHookInitialize failed: 0x%08X\n", status);
		return status;
	}
	DbgPrintEx(77, 0, "[DbgHook] AMD-V virtualized.\n");

	PDEVICE_OBJECT DeviceObject = NULL;
	UNICODE_STRING DriverName, DosDeviceName;
	RtlInitUnicodeString(&DriverName, L"\\Device\\YCData");
	RtlInitUnicodeString(&DosDeviceName, L"\\DosDevices\\YCData");

	status = IoCreateDevice(Driver, 0, &DriverName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status))
	{
		NptHookUninitialize();
		return status;
	}

	Driver->MajorFunction[IRP_MJ_CLOSE] = DrvComm;
	Driver->MajorFunction[IRP_MJ_CREATE] = DrvComm;
	Driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DrvIOCTLDispatcher;
	Driver->Flags |= DO_BUFFERED_IO;

	IoCreateSymbolicLink(&DosDeviceName, &DriverName);

	return STATUS_SUCCESS;
}
