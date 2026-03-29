#pragma once
#ifndef GUI_H
#define GUI_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <propidl.h>
#include <unknwn.h>

#define GDIPVER 0x0110
#include <gdiplus.h>

#include "process.h"
#include "memory.h"
#include "hexdump.h"
#include "utils.h"

#define TIMER_ANIM     1
#define TIMER_REFRESH  2

#define SIDEBAR_W    270
#define STATUSBAR_H   30
#define TOOLBAR_H     46
#define DROPZONE_H   190

#define CLR_BG        RGB(0x0D, 0x0D, 0x0D)
#define CLR_PANEL     RGB(0x11, 0x11, 0x15)
#define CLR_ACCENT    RGB(0x00, 0xAA, 0xFF)
#define CLR_TEXT      RGB(0xFF, 0xFF, 0xFF)
#define CLR_SUBTEXT   RGB(0x88, 0x88, 0x99)
#define CLR_BORDER    RGB(0x28, 0x28, 0x30)
#define CLR_HOVER     RGB(0x1A, 0x1A, 0x22)
#define CLR_SEL       RGB(0x00, 0x28, 0x3A)
#define CLR_SUCCESS   RGB(0x22, 0xCC, 0x66)
#define CLR_WARNING   RGB(0xFF, 0x99, 0x00)
#define CLR_ERR       RGB(0xFF, 0x44, 0x44)

typedef enum {
    PHASE_FADE_IN   = 0,
    PHASE_IDLE      = 1,
    PHASE_ATTACHING = 2,
    PHASE_SCANNING  = 3,
    PHASE_VIEWING   = 4
} AppPhase;

typedef struct {
    float dashOffset;
    float glowAlpha;
    BOOL  glowUp;
    BOOL  isHovered;
} DropAnim;

typedef struct {
    RECT  rc;
    char  label[64];
    BOOL  hovered;
    BOOL  enabled;
    float rippleRadius;
    float rippleAlpha;
    int   rippleX, rippleY;
} ButtonCtx;

typedef struct {
    MemRegion region;
    BOOL      selected;
    float     highlightAlpha;
} SidebarItem;

typedef struct AppCtx {
    HWND      hWnd;
    HINSTANCE hInst;

    ULONG_PTR gdiplusToken;

    HDC     hdcBack;
    HBITMAP hbmBack;
    int     backW, backH;

    AppPhase phase;
    float    fadeAlpha;
    float    fadeTarget;

    DropAnim dropAnim;
    RECT     rcDropZone;

    ButtonCtx btnAttach;
    ButtonCtx btnDetach;
    ButtonCtx btnFilter;
    ButtonCtx btnSearch;
    ButtonCtx btnExport;

    SidebarItem sideItems[MEM_MAX_REGIONS];
    int         sideCount;
    int         sideScroll;
    int         sideHover;
    RECT        rcSidebar;

    HexViewCtx  hexView;
    RECT        rcHexView;
    BYTE       *dumpBuf;
    SIZE_T      dumpBufSize;
    int         activeRegion;

    char searchPattern[256];
    BOOL searchIsHex;

    char     statusText[512];
    COLORREF statusColor;

    ProcessCtx proc;
    MemCtx     mem;
    LogCtx     log;

    DWORD filterFlags;

    ByteInspector inspector;
    BOOL          showInspector;

    BOOL          showJumpBar;
    char          jumpBuf[32];
    int           jumpLen;

    BOOL          showBookmarks;
} AppCtx;

HWND Gui_CreateMainWindow(HINSTANCE hInst, AppCtx *ctx);

void Gui_Destroy(AppCtx *ctx);

LRESULT CALLBACK Gui_WndProc(HWND hWnd, UINT msg, WPARAM wParam,
                              LPARAM lParam);

void Gui_Paint(AppCtx *ctx, HDC hdc);

void Gui_Layout(AppCtx *ctx, int w, int h);

void Gui_Animate(AppCtx *ctx);

void Gui_RefreshRegions(AppCtx *ctx);

void Gui_SetStatus(AppCtx *ctx, COLORREF color, const char *fmt, ...);

void Gui_RippleButton(ButtonCtx *btn, int x, int y);

void Gui_HandleDrop(AppCtx *ctx, const char *path);

DWORD Gui_PromptPid(HWND hParent);

BOOL Gui_PromptSearch(AppCtx *ctx);

void Gui_ExportDump(AppCtx *ctx);

void Gui_ResizeBackBuffer(AppCtx *ctx, int w, int h);

void Gui_DrawInspector(AppCtx *ctx, HDC hdc);

void Gui_DrawJumpBar(AppCtx *ctx, HDC hdc);

void Gui_OnChar(AppCtx *ctx, WCHAR ch);

void Gui_LoadRegion(AppCtx *ctx, int sideIndex);

#endif
