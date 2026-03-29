#pragma once
#ifndef PROCESS_H
#define PROCESS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include "utils.h"

#define PROC_NAME_MAX  260
#define PROC_LIST_MAX  1024

typedef struct {
    DWORD pid;
    char  name[PROC_NAME_MAX];
    char  exePath[MAX_PATH];
} ProcEntry;

typedef struct {
    DWORD  pid;
    char   name[PROC_NAME_MAX];
    char   exePath[MAX_PATH];
    HANDLE hProcess;
    BOOL   elevated;
} ProcessCtx;

BOOL Process_EnableDebugPrivilege(void);

int  Process_Enumerate(ProcEntry *outArray, int maxCount);

DWORD Process_FindByName(const char *name);

BOOL Process_FindNameByPid(DWORD pid, char *outName, SIZE_T outLen);

DWORD Process_ResolveExeToPid(const char *exePath);

BOOL Process_Attach(ProcessCtx *ctx, DWORD pid, LogCtx *log);

void Process_Detach(ProcessCtx *ctx, LogCtx *log);

#endif
