#pragma once
#ifndef MEMORY_H
#define MEMORY_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "utils.h"

#define MEM_MAX_REGIONS  4096

#define MEMFILT_PRIVATE  0x01
#define MEMFILT_IMAGE    0x02
#define MEMFILT_MAPPED   0x04
#define MEMFILT_ALL      (MEMFILT_PRIVATE | MEMFILT_IMAGE | MEMFILT_MAPPED)

typedef struct {
    LPVOID baseAddress;
    SIZE_T regionSize;
    DWORD  state;
    DWORD  protect;
    DWORD  type;
    char   label[64];
} MemRegion;

typedef struct {
    MemRegion regions[MEM_MAX_REGIONS];
    int       count;
    DWORD     filterFlags;
} MemCtx;

int Mem_ScanRegions(MemCtx *ctx, HANDLE hProcess, DWORD filterFlags,
                    LogCtx *log);

SIZE_T Mem_Read(HANDLE hProcess, LPVOID address, void *buf, SIZE_T size);

typedef void (*MemMatchCb)(LPVOID address, void *userData);
int Mem_Search(MemCtx *ctx, HANDLE hProcess,
               const BYTE *pattern, SIZE_T patLen,
               MemMatchCb matchCb, void *userData, LogCtx *log);

BOOL Mem_ExportRegion(HANDLE hProcess, const MemRegion *region,
                      const char *outPath, LogCtx *log);

BOOL Mem_IsReadable(DWORD protect);

BOOL Mem_ParseAddress(const char *str, ULONG_PTR *out);

int  Mem_FindRegion(const MemCtx *ctx, ULONG_PTR addr);

typedef struct {
    BYTE    raw[8];
    int     count;
    ULONG_PTR addr;

    UINT8   u8;   INT8   i8;
    UINT16  u16;  INT16  i16;
    UINT32  u32;  INT32  i32;
    UINT64  u64;  INT64  i64;
    float   f32;
    double  f64;
    char    asci[9];
} ByteInspector;

void Mem_FillInspector(ByteInspector *bi, const BYTE *buf, int count,
                        ULONG_PTR addr);

#endif
