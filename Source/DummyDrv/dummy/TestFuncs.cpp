#include "TestFuncs.h"

_Function_class_(KSTART_ROUTINE)
VOID TestWorker(PVOID context)
{
    UNREFERENCED_PARAMETER(context);
    DbgPrint("New thread of dummy driver is started\n");
}

VOID StartThread()
{
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, 0, OBJ_KERNEL_HANDLE, 0, 0);
    HANDLE hThread = NULL;
    NTSTATUS status = PsCreateSystemThread(&hThread,
        (ACCESS_MASK)(0L),
        &oa,
        NULL,
        NULL,
        TestWorker,
        NULL
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("Cannot create thread, status %d\n", status);
    }

    if (hThread)
    {
        DbgPrint("New thread is created successfully\n");
        ZwClose(hThread);
    }
}


VOID OpenReg()
{
    UNICODE_STRING regPath;
    RtlInitUnicodeString(&regPath, L"\\REGISTRY\\MACHINE\\");

    HANDLE hKey = NULL;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &regPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, 0, 0);

    NTSTATUS status = ZwOpenKey(&hKey, KEY_READ, &oa);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("Cannot open reg path %wZ, status = %d\n", regPath, status);
        return;
    }
    if (hKey)
    {
        DbgPrint("Registry path is opened successfully, path: %wZ\n", regPath);
        ZwClose(hKey);
    }
}

VOID OpenFile()
{
    UNICODE_STRING fileName;
    RtlInitUnicodeString(&fileName, L"\\??\\globalroot\\systemroot\\system32\\calc.exe");

    IO_STATUS_BLOCK statusBlock = { 0 };
    OBJECT_ATTRIBUTES fileAttr = { 0 };

    InitializeObjectAttributes(&fileAttr
        , &fileName
        , OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE
        , NULL
        , NULL);

    LARGE_INTEGER allocSize;
    allocSize.QuadPart = 0;

    HANDLE handle = NULL;
    NTSTATUS status = ZwCreateFile(&handle
        , GENERIC_READ
        , &fileAttr
        , &statusBlock
        , NULL
        , FILE_ATTRIBUTE_NORMAL
        , FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
        , FILE_OPEN
        , FILE_SYNCHRONOUS_IO_ALERT | FILE_NON_DIRECTORY_FILE
        , NULL
        , 0);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("Cannot open file %wZ, status %d\n", fileName, status);
        return;
    }

    if (handle)
    {
        DbgPrint("File %wZ was opened successfully, status %d\n", fileName, status);
        ZwClose(handle);
    }
}

VOID LoadDriver()
{
    UNICODE_STRING driverName;
    RtlInitUnicodeString(&driverName, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\dummy");

    NTSTATUS status = ZwLoadDriver(&driverName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("Cannot load dummy driver, status = %d\n", status);
    }
    else
    {
        DbgPrint("Dummy driver loaded successfully\n");
    }
}

//Unload is used when dummy driver is installed and loaded in legal way
VOID Unload(PDRIVER_OBJECT pDriverObject)
{
    UNREFERENCED_PARAMETER(pDriverObject);
}