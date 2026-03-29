#include "../include/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Utils_InitLog(LogCtx *ctx, const char *optPath)
{
    if (!ctx) return;
    ctx->enabled  = TRUE;
    ctx->hLogFile = INVALID_HANDLE_VALUE;

    if (optPath && optPath[0]) {
        ctx->hLogFile = CreateFileA(
            optPath, GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
}

void Utils_CloseLog(LogCtx *ctx)
{
    if (!ctx) return;
    if (ctx->hLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->hLogFile);
        ctx->hLogFile = INVALID_HANDLE_VALUE;
    }
    ctx->enabled = FALSE;
}

void Utils_Log(LogCtx *ctx, LogLevel level, const char *fmt, ...)
{
    if (!ctx || !ctx->enabled || !fmt) return;

    static const char *labels[] = { "INFO", "WARN", "ERROR" };
    const char *lbl = (level <= LOG_ERROR) ? labels[level] : "????";

    char body[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1152];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "[%02u:%02u:%02u] [%s] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, lbl, body);

    if (ctx->hLogFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(ctx->hLogFile, line, (DWORD)strlen(line), &written, NULL);
    }
    OutputDebugStringA(line);
}

void Utils_FormatError(DWORD code, char *dst, SIZE_T dstLen)
{
    if (!dst || dstLen == 0) return;
    char *msg = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg, 0, NULL);

    if (msg) {
        SIZE_T len = strlen(msg);
        while (len > 0 && (msg[len-1] == '\r' || msg[len-1] == '\n'))
            msg[--len] = '\0';
        _snprintf_s(dst, dstLen, _TRUNCATE, "%s (0x%08X)", msg, code);
        LocalFree(msg);
    } else {
        _snprintf_s(dst, dstLen, _TRUNCATE, "Error 0x%08X", code);
    }
}

void Utils_FormatLastError(char *dst, SIZE_T dstLen)
{
    Utils_FormatError(GetLastError(), dst, dstLen);
}

int Utils_SNPrintf(char *dst, SIZE_T dstLen, const char *fmt, ...)
{
    if (!dst || dstLen == 0) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(dst, dstLen, _TRUNCATE, fmt, ap);
    va_end(ap);
    return (n < 0) ? (int)dstLen - 1 : n;
}

char *Utils_WideToUtf8(const WCHAR *wide)
{
    if (!wide) return NULL;
    int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                                    NULL, 0, NULL, NULL);
    if (need <= 0) return NULL;
    char *out = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)need);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, need, NULL, NULL);
    return out;
}

WCHAR *Utils_Utf8ToWide(const char *utf8)
{
    if (!utf8) return NULL;
    int need = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (need <= 0) return NULL;
    WCHAR *out = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
                                     (SIZE_T)need * sizeof(WCHAR));
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, need);
    return out;
}

BOOL Utils_WideToNarrow(const WCHAR *src, char *dst, int dstLen)
{
    if (!src || !dst || dstLen <= 0) return FALSE;
    int r = WideCharToMultiByte(CP_UTF8, 0, src, -1,
                                 dst, dstLen, NULL, NULL);
    if (r <= 0) { dst[0] = '\0'; return FALSE; }
    return TRUE;
}

BOOL Utils_NarrowToWide(const char *src, WCHAR *dst, int dstLen)
{
    if (!src || !dst || dstLen <= 0) return FALSE;
    int r = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstLen);
    if (r <= 0) { dst[0] = L'\0'; return FALSE; }
    return TRUE;
}

static const char HEX[] = "0123456789ABCDEF";

void Utils_BytesToHex(const BYTE *buf, SIZE_T len,
                       char *out, SIZE_T outLen)
{
    SIZE_T pos = 0;
    for (SIZE_T i = 0; i < len && pos + 3 < outLen; i++) {
        out[pos++] = HEX[(buf[i] >> 4) & 0xF];
        out[pos++] = HEX[ buf[i]       & 0xF];
        if (i + 1 < len) out[pos++] = ' ';
    }
    out[pos < outLen ? pos : outLen - 1] = '\0';
}

static int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

BOOL Utils_HexToBytes(const char *hex, BYTE *out, SIZE_T *outLen)
{
    if (!hex || !out || !outLen) return FALSE;
    SIZE_T wrote = 0, i = 0, slen = strlen(hex);
    while (i < slen) {
        while (i < slen && hex[i] == ' ') i++;
        if (i >= slen) break;
        int hi = HexVal(hex[i]);
        int lo = (i + 1 < slen) ? HexVal(hex[i + 1]) : -1;
        if (hi < 0 || lo < 0) return FALSE;
        out[wrote++] = (BYTE)((hi << 4) | lo);
        i += 2;
    }
    *outLen = wrote;
    return (wrote > 0);
}

const char *Utils_MemTypeStr(DWORD type)
{
    switch (type) {
    case MEM_IMAGE:   return "IMG";
    case MEM_MAPPED:  return "MAP";
    case MEM_PRIVATE: return "PRV";
    default:          return "???";
    }
}

const char *Utils_MemStateStr(DWORD state)
{
    switch (state) {
    case MEM_COMMIT:  return "COMMIT ";
    case MEM_FREE:    return "FREE   ";
    case MEM_RESERVE: return "RESERVE";
    default:          return "???????";
    }
}

const char *Utils_MemProtectStr(DWORD protect)
{
    DWORD base = protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    switch (base) {
    case PAGE_NOACCESS:           return "---";
    case PAGE_READONLY:           return "R--";
    case PAGE_READWRITE:          return "RW-";
    case PAGE_WRITECOPY:          return "RC-";
    case PAGE_EXECUTE:            return "--X";
    case PAGE_EXECUTE_READ:       return "R-X";
    case PAGE_EXECUTE_READWRITE:  return "RWX";
    case PAGE_EXECUTE_WRITECOPY:  return "RCX";
    default:                      return "???";
    }
}

int Utils_Clamp(int val, int lo, int hi)
{
    return val < lo ? lo : val > hi ? hi : val;
}

float Utils_Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
