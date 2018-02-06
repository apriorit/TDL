#pragma once

#include <ntifs.h>

VOID TestWorker(PVOID context);
VOID StartThread();
VOID OpenReg();
VOID OpenFile();
VOID LoadDriver();
VOID Unload(PDRIVER_OBJECT pDriverObject);
