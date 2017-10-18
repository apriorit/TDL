/*******************************************************************************
*
*  (C) COPYRIGHT AUTHORS, 2016 - 2017
*
*  TITLE:       MAIN.C
*
*  VERSION:     1.01
*
*  DATE:        20 Apr 2017
*
*  "Driverless" example #2
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
* TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
* PARTICULAR PURPOSE.
*
*******************************************************************************/
#include <ntddk.h>
#include "main.h"

#define DEBUGPRINT

/*
* PrintIrql
*
* Purpose:
*
* Debug print current irql.
*
*/
VOID PrintIrql()
{
    KIRQL Irql;
    PWSTR sIrql;

    Irql = KeGetCurrentIrql();

    switch (Irql) {

    case PASSIVE_LEVEL:
        sIrql = L"PASSIVE_LEVEL";
        break;
    case APC_LEVEL:
        sIrql = L"APC_LEVEL";
        break;
    case DISPATCH_LEVEL:
        sIrql = L"DISPATCH_LEVEL";
        break;
    case CMCI_LEVEL:
        sIrql = L"CMCI_LEVEL";
        break;
    case CLOCK_LEVEL:
        sIrql = L"CLOCK_LEVEL";
        break;
    case IPI_LEVEL:
        sIrql = L"IPI_LEVEL";
        break;
    case HIGH_LEVEL:
        sIrql = L"HIGH_LEVEL";
        break;
    default:
        sIrql = L"Unknown Value";
        break;
    }
    DbgPrint("KeGetCurrentIrql=%u(%ws)\n", Irql, sIrql);
}

/*
* DevioctlDispatch
*
* Purpose:
*
* IRP_MJ_DEVICE_CONTROL dispatch.
*
*/
NTSTATUS DevioctlDispatch(
	_In_ struct _DEVICE_OBJECT *DeviceObject,
	_Inout_ struct _IRP *Irp
	)
{
	NTSTATUS				status = STATUS_SUCCESS;
	ULONG					bytesIO = 0;
	PIO_STACK_LOCATION		stack;
	BOOLEAN					condition = FALSE;
	PINOUTPARAM             rp, wp;

	UNREFERENCED_PARAMETER(DeviceObject);

#ifdef DEBUGPRINT
	DbgPrint("%s IRP_MJ_DEVICE_CONTROL", __FUNCTION__);
#endif

	stack = IoGetCurrentIrpStackLocation(Irp);

	do {

		if (stack == NULL) {
			status = STATUS_INTERNAL_ERROR;
			break;
		}

		rp = (PINOUTPARAM)Irp->AssociatedIrp.SystemBuffer;
		wp = (PINOUTPARAM)Irp->AssociatedIrp.SystemBuffer;
		if (rp == NULL) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		switch (stack->Parameters.DeviceIoControl.IoControlCode) {
		case DUMMYDRV_REQUEST1:

#ifdef DEBUGPRINT
			DbgPrint("%s DUMMYDRV_REQUEST1 hit", __FUNCTION__);
#endif			
			if (stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(INOUT_PARAM)) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

#ifdef DEBUGPRINT
			DbgPrint("%s in params = %lx, %lx, %lx, %lx", __FUNCTION__, 
				rp->Param1, rp->Param2, rp->Param3, rp->Param4);
#endif

			wp->Param1 = 11111111;
			wp->Param2 = 22222222;
			wp->Param3 = 33333333;
			wp->Param4 = 44444444;

			status = STATUS_SUCCESS;
			bytesIO = sizeof(INOUT_PARAM);

			break;

		default:

#ifdef DEBUGPRINT
			DbgPrint("%s hit with invalid IoControlCode", __FUNCTION__);
#endif
			status = STATUS_INVALID_PARAMETER;
		};

	} while (condition);

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = bytesIO;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

/*
* UnsupportedDispatch
*
* Purpose:
*
* Unused IRP_MJ_* dispatch.
*
*/
NTSTATUS UnsupportedDispatch(
	_In_ struct _DEVICE_OBJECT *DeviceObject,
	_Inout_ struct _IRP *Irp
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Irp->IoStatus.Status;
}

/*
* CreateDispatch
*
* Purpose:
*
* IRP_MJ_CREATE dispatch.
*
*/
NTSTATUS CreateDispatch(
	_In_ struct _DEVICE_OBJECT *DeviceObject,
	_Inout_ struct _IRP *Irp
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

#ifdef DEBUGPRINT
	DbgPrint("%s Create", __FUNCTION__);
#endif

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Irp->IoStatus.Status;
}

/*
* CloseDispatch
*
* Purpose:
*
* IRP_MJ_CLOSE dispatch.
*
*/
NTSTATUS CloseDispatch(
	_In_ struct _DEVICE_OBJECT *DeviceObject,
	_Inout_ struct _IRP *Irp
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

#ifdef DEBUGPRINT
	DbgPrint("%s Close", __FUNCTION__);
#endif

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Irp->IoStatus.Status;
}

//
// Create thread function
//
_Function_class_(KSTART_ROUTINE)
static VOID ThreadWorker(PVOID context)
{
    DbgPrint("Thread created by DUMMY driver.\n");
}

void DummyCreateThread()
{
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, 0, OBJ_KERNEL_HANDLE, 0, 0);

    HANDLE hThread = NULL;
    auto status = PsCreateSystemThread(&hThread,
        0L,
        &oa,
        NULL,
        NULL,
        ThreadWorker,
        NULL);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("DUMMY: Thread creation faild.\n");
        return;
    }

    ZwClose(hThread);
}

_Function_class_(IO_WORKITEM_ROUTINE_EX)
static VOID WorkerRoutine(_In_ PVOID IoObject, _In_opt_ PVOID Context, _In_ PIO_WORKITEM IoWorkItem)
{
    UNREFERENCED_PARAMETER(IoObject);
    UNREFERENCED_PARAMETER(Context);
    IoFreeWorkItem(IoWorkItem);

    NTSTATUS        status;
    UNICODE_STRING  drvName;

    PrintIrql();

#ifdef DEBUGPRINT
    DbgPrint("%s\n", __FUNCTION__);
#endif

    RtlInitUnicodeString(&drvName, L"\\Driver\\dummyNormal");
    status = IoCreateDriver(&drvName, &DriverInitialize);

#ifdef DEBUGPRINT
    DbgPrint("%s IoCreateDriver(%wZ) = %lx\n", __FUNCTION__, drvName, status);
#endif
}

/*
* DriverInitialize
*
* Purpose:
*
* Driver main.
*
*/
NTSTATUS DriverInitialize(
	_In_  struct _DRIVER_OBJECT *DriverObject,
	_In_  PUNICODE_STRING RegistryPath
	)
{
    NTSTATUS        status;
    UNICODE_STRING  SymLink, DevName;
    PDEVICE_OBJECT  devobj;
    ULONG           t;

#ifdef DEBUGPRINT
    DbgPrint("%s\n", __FUNCTION__);
#endif

    RtlInitUnicodeString(&DevName, L"\\Device\\dummyNormal");
    status = IoCreateDevice(DriverObject, 0, &DevName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, TRUE, &devobj);

#ifdef DEBUGPRINT
    DbgPrint("%s IoCreateDevice(%wZ) = %lx\n", __FUNCTION__, DevName, status);
#endif

    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&SymLink, L"\\DosDevices\\TDLD");
    status = IoCreateSymbolicLink(&SymLink, &DevName);

#ifdef DEBUGPRINT
    DbgPrint("%s IoCreateSymbolicLink(%wZ) = %lx\n", __FUNCTION__, SymLink, status);
#endif

    devobj->Flags |= DO_BUFFERED_IO;

    for (t = 0; t <= IRP_MJ_MAXIMUM_FUNCTION; t++)
        DriverObject->MajorFunction[t] = &UnsupportedDispatch;

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = &DevioctlDispatch;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = &CreateDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = &CloseDispatch;
    DriverObject->DriverUnload = NULL; //nonstandard way of driver loading, no unload

                                       //DummyCreateThread();

    devobj->Flags &= ~DO_DEVICE_INITIALIZING;

    return status;
}


/*
* DriverEntry
*
* Purpose:
*
* Driver base entry point.
*
*/
NTSTATUS DriverEntry(
  _In_  struct _DRIVER_OBJECT *DriverObject,
  _In_  PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    PIO_WORKITEM workItem = IoAllocateWorkItem(PsGetCurrentProcess());
    if (!workItem)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoQueueWorkItemEx(workItem
        , WorkerRoutine
        , DelayedWorkQueue
        , NULL);


    return STATUS_SUCCESS;
}

