#pragma once
#ifndef HEXDUMP_H
#define HEXDUMP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define HEX_BYTES_PER_ROW  16
#define HEX_MAX_ROWS       16384

typedef struct {
    ULONG_PTR offset;
    BYTE      bytes[HEX_BYTES_PER_ROW];
    int       byteCount;
    char      addrStr[20];
    char      hexStr[HEX_BYTES_PER_ROW * 3 + 2];
    char      asciiStr[HEX_BYTES_PER_ROW + 2];
} HexRow;

#define HEX_MAX_BOOKMARKS 32

typedef struct {
    ULONG_PTR addr;
    char      label[48];
    COLORREF  color;
} HexBookmark;

typedef struct {
    HexRow rows[HEX_MAX_ROWS];
    int    rowCount;

    int    scrollRow;
    float  scrollOffset;
    float  scrollTarget;

    int    hoveredRow;
    int    selectedRow;

    HFONT  hFont;
    int    rowHeight;
    int    visibleRows;
    int    charWidth;

    COLORREF clrBg;
    COLORREF clrText;
    COLORREF clrAddr;
    COLORREF clrHex;
    COLORREF clrAscii;
    COLORREF clrHover;
    COLORREF clrSelected;
    COLORREF clrAccent;

    HexBookmark bookmarks[HEX_MAX_BOOKMARKS];
    int         bookmarkCount;
} HexViewCtx;

int  Hex_BuildRows(HexViewCtx *ctx, const BYTE *buf, SIZE_T bufLen,
                   ULONG_PTR virtualBase);

void Hex_InitView(HexViewCtx *ctx, HDC hdc, int rowHeight);

void Hex_DestroyView(HexViewCtx *ctx);

void Hex_Paint(HexViewCtx *ctx, HDC hdc, const RECT *rc);

BOOL Hex_MouseMove(HexViewCtx *ctx, int mouseY, const RECT *rc);

BOOL Hex_MouseWheel(HexViewCtx *ctx, int delta);

BOOL Hex_AnimateScroll(HexViewCtx *ctx);

void Hex_RowToText(const HexRow *row, char *dst, SIZE_T dstLen);

void Hex_AddBookmark(HexViewCtx *ctx, ULONG_PTR addr, const char *label);
void Hex_RemoveBookmark(HexViewCtx *ctx, ULONG_PTR addr);
const HexBookmark *Hex_GetBookmark(const HexViewCtx *ctx, ULONG_PTR addr);
BOOL Hex_JumpToAddress(HexViewCtx *ctx, ULONG_PTR addr);
BOOL Hex_CopyToClipboard(HexViewCtx *ctx, HWND hWnd);

#endif
