#include "../include/process.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

BOOL Process_EnableDebugPrivilege(void)
{
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hToken))
        return FALSE;

    LUID luid;
    if (!LookupPrivilegeValueA(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return FALSE;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount           = 1;
    tp.Privileges[0].Luid       = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    DWORD err = GetLastError();
    CloseHandle(hToken);
    return (err != ERROR_NOT_ALL_ASSIGNED);
}

int Process_Enumerate(ProcEntry *outArray, int maxCount)
{
    if (!outArray || maxCount <= 0) return -1;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return -1;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    int count = 0;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (count >= maxCount) break;
            ProcEntry *e = &outArray[count];
            e->pid = pe.th32ProcessID;
            Utils_WideToNarrow(pe.szExeFile, e->name, PROC_NAME_MAX);

            e->exePath[0] = '\0';
            HANDLE hProc = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e->pid);
            if (hProc) {
                WCHAR wpath[MAX_PATH];
                DWORD plen = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, wpath, &plen))
                    Utils_WideToNarrow(wpath, e->exePath, MAX_PATH);
                CloseHandle(hProc);
            }
            count++;
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return count;
}

DWORD Process_FindByName(const char *name)
{
    if (!name) return 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD found = 0;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            char narrow[PROC_NAME_MAX];
            Utils_WideToNarrow(pe.szExeFile, narrow, PROC_NAME_MAX);
            if (_stricmp(narrow, name) == 0) {
                found = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return found;
}

BOOL Process_FindNameByPid(DWORD pid, char *outName, SIZE_T outLen)
{
    if (!outName || outLen == 0) return FALSE;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        _snprintf_s(outName, outLen, _TRUNCATE, "<PID %u>", pid);
        return FALSE;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    BOOL found = FALSE;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                Utils_WideToNarrow(pe.szExeFile, outName, (int)outLen);
                found = TRUE;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);

    if (!found)
        _snprintf_s(outName, outLen, _TRUNCATE, "<PID %u>", pid);

    return found;
}

DWORD Process_ResolveExeToPid(const char *exePath)
{
    if (!exePath) return 0;

    const char *base = strrchr(exePath, '\\');
    base = base ? base + 1 : exePath;

    return Process_FindByName(base);
}

BOOL Process_Attach(ProcessCtx *ctx, DWORD pid, LogCtx *log)
{
    if (!ctx) return FALSE;

    ZeroMemory(ctx, sizeof(*ctx));
    ctx->pid = pid;

    ctx->hProcess = OpenProcess(
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);

    if (!ctx->hProcess) {
        ctx->hProcess = OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, pid);
    }

    if (!ctx->hProcess) {
        char err[256];
        Utils_FormatLastError(err, sizeof(err));
        Utils_Log(log, LOG_ERROR,
                  "OpenProcess(%u) failed: %s", pid, err);
        return FALSE;
    }

    Process_FindNameByPid(pid, ctx->name, PROC_NAME_MAX);

    WCHAR wpath[MAX_PATH];
    DWORD plen = MAX_PATH;
    if (QueryFullProcessImageNameW(ctx->hProcess, 0, wpath, &plen))
        Utils_WideToNarrow(wpath, ctx->exePath, MAX_PATH);

    Utils_Log(log, LOG_INFO,
              "Attached to PID %u (%s)", pid, ctx->name);
    return TRUE;
}

void Process_Detach(ProcessCtx *ctx, LogCtx *log)
{
    if (!ctx) return;
    if (ctx->hProcess) {
        Utils_Log(log, LOG_INFO,
                  "Detached from PID %u (%s)", ctx->pid, ctx->name);
        CloseHandle(ctx->hProcess);
    }
    ZeroMemory(ctx, sizeof(*ctx));
}
