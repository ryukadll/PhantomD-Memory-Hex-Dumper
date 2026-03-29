#pragma once
#ifndef UTILS_H
#define UTILS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>

typedef enum { LOG_INFO = 0, LOG_WARN = 1, LOG_ERROR = 2 } LogLevel;

typedef struct {
    BOOL   enabled;
    HANDLE hLogFile;
} LogCtx;

void Utils_InitLog(LogCtx *ctx, const char *optPath);
void Utils_CloseLog(LogCtx *ctx);

void Utils_Log(LogCtx *ctx, LogLevel level, const char *fmt, ...);

void Utils_FormatError(DWORD code, char *dst, SIZE_T dstLen);
void Utils_FormatLastError(char *dst, SIZE_T dstLen);

int  Utils_SNPrintf(char *dst, SIZE_T dstLen, const char *fmt, ...);

char  *Utils_WideToUtf8(const WCHAR *wide);
WCHAR *Utils_Utf8ToWide(const char  *utf8);

BOOL Utils_WideToNarrow(const WCHAR *src, char *dst, int dstLen);
BOOL Utils_NarrowToWide(const char  *src, WCHAR *dst, int dstLen);

void Utils_BytesToHex(const BYTE *buf, SIZE_T len, char *out, SIZE_T outLen);
BOOL Utils_HexToBytes(const char *hex, BYTE *out, SIZE_T *outLen);

const char *Utils_MemTypeStr(DWORD type);
const char *Utils_MemStateStr(DWORD state);
const char *Utils_MemProtectStr(DWORD protect);

int   Utils_Clamp(int val, int lo, int hi);
float Utils_Lerp(float a, float b, float t);

#endif
