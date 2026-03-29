#include "../include/memory.h"
#include <string.h>
#include <stdio.h>

#define SEARCH_CHUNK_SIZE (4UL * 1024UL * 1024UL)

BOOL Mem_IsReadable(DWORD protect)
{
    if (protect == 0)              return FALSE;
    if (protect & PAGE_NOACCESS)   return FALSE;
    if (protect & PAGE_GUARD)      return FALSE;

    DWORD base = protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return (base == PAGE_READONLY          ||
            base == PAGE_READWRITE         ||
            base == PAGE_WRITECOPY         ||
            base == PAGE_EXECUTE_READ      ||
            base == PAGE_EXECUTE_READWRITE ||
            base == PAGE_EXECUTE_WRITECOPY);
}

int Mem_ScanRegions(MemCtx *ctx, HANDLE hProcess,
                    DWORD filterFlags, LogCtx *log)
{
    if (!ctx || !hProcess) return -1;

    ctx->count       = 0;
    ctx->filterFlags = filterFlags;

    LPVOID addr = NULL;
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T totalBytes = 0;

    while (VirtualQueryEx(hProcess, addr, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        BOOL typeOk = FALSE;
        if ((filterFlags & MEMFILT_PRIVATE) && mbi.Type == MEM_PRIVATE) typeOk = TRUE;
        if ((filterFlags & MEMFILT_IMAGE)   && mbi.Type == MEM_IMAGE  ) typeOk = TRUE;
        if ((filterFlags & MEMFILT_MAPPED)  && mbi.Type == MEM_MAPPED ) typeOk = TRUE;

        if (mbi.State == MEM_COMMIT && Mem_IsReadable(mbi.Protect) && typeOk) {
            if (ctx->count < MEM_MAX_REGIONS) {
                MemRegion *r = &ctx->regions[ctx->count];
                r->baseAddress = mbi.BaseAddress;
                r->regionSize  = mbi.RegionSize;
                r->state       = mbi.State;
                r->protect     = mbi.Protect;
                r->type        = mbi.Type;

                _snprintf_s(r->label, sizeof(r->label), _TRUNCATE,
                    "%s %s  %6zu KB",
                    Utils_MemTypeStr(mbi.Type),
                    Utils_MemProtectStr(mbi.Protect),
                    mbi.RegionSize / 1024);

                ctx->count++;
                totalBytes += mbi.RegionSize;
            }
        }

        LPVOID next = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }

    Utils_Log(log, LOG_INFO,
              "Scan: %d regions, %zu KB readable",
              ctx->count, totalBytes / 1024);
    return ctx->count;
}

SIZE_T Mem_Read(HANDLE hProcess, LPVOID address,
                void *buf, SIZE_T size)
{
    if (!hProcess || !buf || size == 0) return 0;
    SIZE_T got = 0;
    ReadProcessMemory(hProcess, address, buf, size, &got);
    return got;
}

int Mem_Search(MemCtx *ctx, HANDLE hProcess,
               const BYTE *pattern, SIZE_T patLen,
               MemMatchCb matchCb, void *userData,
               LogCtx *log)
{
    if (!ctx || !hProcess || !pattern || patLen == 0) return 0;

    BYTE *chunk = (BYTE *)VirtualAlloc(NULL, SEARCH_CHUNK_SIZE,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE);
    if (!chunk) {
        Utils_Log(log, LOG_ERROR, "Search: out of memory for chunk buffer");
        return 0;
    }

    int totalHits = 0;

    for (int ri = 0; ri < ctx->count; ri++) {
        MemRegion *reg = &ctx->regions[ri];
        SIZE_T remaining = reg->regionSize;
        BYTE  *addr      = (BYTE *)reg->baseAddress;

        while (remaining > 0) {
            SIZE_T toRead = remaining > SEARCH_CHUNK_SIZE
                            ? SEARCH_CHUNK_SIZE : remaining;
            SIZE_T got = 0;

            if (!ReadProcessMemory(hProcess, addr, chunk, toRead, &got)
                || got == 0)
                break;

            for (SIZE_T i = 0; i + patLen <= got; i++) {
                if (memcmp(chunk + i, pattern, patLen) == 0) {
                    if (matchCb)
                        matchCb((LPVOID)(addr + i), userData);
                    totalHits++;
                }
            }

            addr      += got;
            remaining -= got;
        }
    }

    VirtualFree(chunk, 0, MEM_RELEASE);

    Utils_Log(log, LOG_INFO, "Search complete: %d hit(s)", totalHits);
    return totalHits;
}

BOOL Mem_ExportRegion(HANDLE hProcess, const MemRegion *region,
                      const char *outPath, LogCtx *log)
{
    if (!hProcess || !region || !outPath) return FALSE;

    HANDLE hFile = CreateFileA(outPath, GENERIC_WRITE, 0,
                                NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        char err[256];
        Utils_FormatLastError(err, sizeof(err));
        Utils_Log(log, LOG_ERROR,
                  "ExportRegion: cannot create '%s': %s", outPath, err);
        return FALSE;
    }

    const SIZE_T CHUNK = 1024UL * 1024UL;
    BYTE *buf = (BYTE *)VirtualAlloc(NULL, CHUNK,
                                      MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (!buf) {
        CloseHandle(hFile);
        return FALSE;
    }

    SIZE_T remaining = region->regionSize;
    BYTE  *addr      = (BYTE *)region->baseAddress;
    BOOL   ok        = TRUE;

    while (remaining > 0) {
        SIZE_T toRead = remaining > CHUNK ? CHUNK : remaining;
        SIZE_T got    = 0;

        ReadProcessMemory(hProcess, addr, buf, toRead, &got);
        if (got == 0) {
            ZeroMemory(buf, toRead);
            got = toRead;
        }

        DWORD written;
        if (!WriteFile(hFile, buf, (DWORD)got, &written, NULL)) {
            ok = FALSE;
            break;
        }

        addr      += got;
        remaining -= got;
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(hFile);

    if (ok)
        Utils_Log(log, LOG_INFO,
                  "Exported %zu bytes from %p to '%s'",
                  region->regionSize, region->baseAddress, outPath);
    return ok;
}

BOOL Mem_ParseAddress(const char *str, ULONG_PTR *out)
{
    if (!str || !out) return FALSE;

    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
        str += 2;
    if (!str[0]) return FALSE;

    ULONG_PTR val = 0;
    while (*str) {
        char c = *str++;
        ULONG_PTR digit;
        if (c >= '0' && c <= '9')      digit = (ULONG_PTR)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (ULONG_PTR)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (ULONG_PTR)(c - 'A' + 10);
        else return FALSE;
        val = (val << 4) | digit;
    }
    *out = val;
    return TRUE;
}

int Mem_FindRegion(const MemCtx *ctx, ULONG_PTR addr)
{
    if (!ctx) return -1;
    for (int i = 0; i < ctx->count; i++) {
        ULONG_PTR base = (ULONG_PTR)ctx->regions[i].baseAddress;
        ULONG_PTR end  = base + ctx->regions[i].regionSize;
        if (addr >= base && addr < end)
            return i;
    }
    return -1;
}

void Mem_FillInspector(ByteInspector *bi, const BYTE *buf, int count,
                        ULONG_PTR addr)
{
    if (!bi || !buf || count <= 0) return;
    ZeroMemory(bi, sizeof(*bi));
    bi->addr  = addr;
    bi->count = count > 8 ? 8 : count;
    memcpy(bi->raw, buf, (SIZE_T)bi->count);

    bi->u8  = bi->raw[0];
    bi->i8  = (INT8)bi->raw[0];

    if (bi->count >= 2) { memcpy(&bi->u16, bi->raw, 2); bi->i16 = (INT16)bi->u16; }
    if (bi->count >= 4) { memcpy(&bi->u32, bi->raw, 4); bi->i32 = (INT32)bi->u32;
                          memcpy(&bi->f32, bi->raw, 4); }
    if (bi->count >= 8) { memcpy(&bi->u64, bi->raw, 8); bi->i64 = (INT64)bi->u64;
                          memcpy(&bi->f64, bi->raw, 8); }

    for (int i = 0; i < bi->count; i++) {
        BYTE b = bi->raw[i];
        bi->asci[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
    }
    bi->asci[bi->count] = '\0';
}
