#include "../include/hexdump.h"
#include "../include/utils.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void Hex_InitView(HexViewCtx *ctx, HDC hdc, int rowHeight)
{
    if (!ctx) return;
    ZeroMemory(ctx, sizeof(*ctx));
    ctx->rowHeight   = (rowHeight > 0) ? rowHeight : 18;
    ctx->hoveredRow  = -1;
    ctx->selectedRow = -1;
    ctx->clrBg       = RGB(0x0D, 0x0D, 0x0D);
    ctx->clrText     = RGB(0xE0, 0xE0, 0xE0);
    ctx->clrAddr     = RGB(0x00, 0xAA, 0xFF);
    ctx->clrHex      = RGB(0xCC, 0xCC, 0xCC);
    ctx->clrAscii    = RGB(0x55, 0xBB, 0x55);
    ctx->clrHover    = RGB(0x18, 0x22, 0x30);
    ctx->clrSelected = RGB(0x00, 0x2A, 0x44);
    ctx->clrAccent   = RGB(0x00, 0xAA, 0xFF);

    static const char *fonts[] = {
        "Cascadia Mono","Cascadia Code","Consolas","Courier New",NULL };
    int fi;
    for (fi = 0; fonts[fi]; fi++) {
        ctx->hFont = CreateFontA(ctx->rowHeight-2,0,0,0,FW_NORMAL,
            FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,fonts[fi]);
        if (ctx->hFont) break;
    }
    if (ctx->hFont && hdc) {
        HFONT old = (HFONT)SelectObject(hdc, ctx->hFont);
        TEXTMETRICA tm; GetTextMetricsA(hdc, &tm);
        ctx->charWidth = tm.tmAveCharWidth;
        ctx->rowHeight = tm.tmHeight + 4;
        SelectObject(hdc, old);
    } else { ctx->charWidth = 8; }
}

void Hex_DestroyView(HexViewCtx *ctx)
{
    if (!ctx) return;
    if (ctx->hFont) { DeleteObject(ctx->hFont); ctx->hFont = NULL; }
}

int Hex_BuildRows(HexViewCtx *ctx, const BYTE *buf, SIZE_T bufLen,
                  ULONG_PTR virtualBase)
{
    if (!ctx) return 0;
    if (!buf || bufLen == 0) {
        ctx->rowCount=0; ctx->scrollRow=0;
        ctx->scrollOffset=0.f; ctx->scrollTarget=0.f;
        ctx->hoveredRow=-1; ctx->selectedRow=-1;
        return 0;
    }
    int rowIdx=0; SIZE_T byteIdx=0;
    while (byteIdx < bufLen && rowIdx < HEX_MAX_ROWS) {
        HexRow *row = &ctx->rows[rowIdx];
        row->offset = virtualBase + (ULONG_PTR)byteIdx;
        int count = 0, i;
        for (i = 0; i < HEX_BYTES_PER_ROW; i++) {
            if (byteIdx+(SIZE_T)i >= bufLen) break;
            row->bytes[i] = buf[byteIdx+(SIZE_T)i]; count++;
        }
        row->byteCount = count;
        _snprintf_s(row->addrStr, sizeof(row->addrStr), _TRUNCATE,
                    "%016IX", row->offset);
        { char *hp=row->hexStr; int left=(int)sizeof(row->hexStr);
          for (i=0;i<HEX_BYTES_PER_ROW;i++){
            int w; if(i<count) w=_snprintf_s(hp,left,_TRUNCATE,"%02X",row->bytes[i]);
            else w=_snprintf_s(hp,left,_TRUNCATE,"  ");
            if(w<0) w=0;
                hp+=w; left-=w;
            if(left>1&&i<HEX_BYTES_PER_ROW-1){*hp++=' ';left--;}
          } if(left>0)*hp='\0'; }
        { char *ap=row->asciiStr;
          for (i=0;i<HEX_BYTES_PER_ROW;i++){
            if(i<count){BYTE b=row->bytes[i];*ap++=(b>=0x20&&b<0x7F)?(char)b:'.';}
            else *ap++=' ';
          } *ap='\0'; }
        byteIdx+=(SIZE_T)count; rowIdx++;
    }
    ctx->rowCount=rowIdx; ctx->scrollRow=0;
    ctx->scrollOffset=0.f; ctx->scrollTarget=0.f;
    ctx->hoveredRow=-1; ctx->selectedRow=-1;
    return rowIdx;
}

void Hex_Paint(HexViewCtx *ctx, HDC hdc, const RECT *rc)
{
    if (!ctx || !hdc || !rc) return;
    int W=rc->right-rc->left, H=rc->bottom-rc->top;
    int rh=ctx->rowHeight, cw=ctx->charWidth?ctx->charWidth:8;
    (void)W;

    { HBRUSH bg=CreateSolidBrush(ctx->clrBg); FillRect(hdc,rc,bg); DeleteObject(bg); }

    if (ctx->rowCount==0 || rh==0) {
        SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,RGB(0x33,0x44,0x55));
        RECT m=*rc;
        DrawTextA(hdc,"No data  --  select a region from the sidebar",
                  -1,&m,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return;
    }

    ctx->visibleRows = (H-rh)/rh;
    if (ctx->visibleRows<1) ctx->visibleRows=1;
    { int ms=ctx->rowCount-ctx->visibleRows; if(ms<0)ms=0;
      if(ctx->scrollRow>ms)ctx->scrollRow=ms;
      if(ctx->scrollRow<0)ctx->scrollRow=0; }

    HFONT oldFont=(HFONT)SelectObject(hdc,ctx->hFont);

    int xAddr = rc->left + 4;
    int xHex  = xAddr + 19*cw;
    int xAsc  = xHex  + (HEX_BYTES_PER_ROW*3)*cw + cw;

    { RECT hr2={rc->left,rc->top,rc->right,rc->top+rh};
      HBRUSH hb=CreateSolidBrush(RGB(0x10,0x10,0x18));
      FillRect(hdc,&hr2,hb); DeleteObject(hb);
      SetBkColor(hdc,RGB(0x10,0x10,0x18));
      SetBkMode(hdc,OPAQUE);
      SetTextColor(hdc,RGB(0x00,0x77,0xAA));
      RECT ht=hr2; ht.left=xAddr;
      DrawTextA(hdc,"Address              00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F   ASCII",
                -1,&ht,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      HPEN sp=CreatePen(PS_SOLID,1,RGB(0x00,0x44,0x66));
      HPEN op=(HPEN)SelectObject(hdc,sp);
      MoveToEx(hdc,rc->left,rc->top+rh,NULL); LineTo(hdc,rc->right,rc->top+rh);
      SelectObject(hdc,op); DeleteObject(sp); }

    HBRUSH brEven   = CreateSolidBrush(RGB(0x0D,0x0D,0x0D));
    HBRUSH brOdd    = CreateSolidBrush(RGB(0x0F,0x0F,0x15));
    HBRUSH brHov    = CreateSolidBrush(RGB(0x18,0x22,0x30));
    HBRUSH brSel    = CreateSolidBrush(RGB(0x00,0x2A,0x44));
    HBRUSH brAccBar = CreateSolidBrush(ctx->clrAccent);
    HPEN   penRow   = CreatePen(PS_SOLID,1,RGB(0x18,0x18,0x22));
    HPEN   penVert  = CreatePen(PS_SOLID,1,RGB(0x22,0x33,0x44));
    HPEN   oldPen   = (HPEN)SelectObject(hdc,penRow);

    int y = rc->top + rh;
    int row;
    for (row=ctx->scrollRow; row<ctx->rowCount && y<rc->bottom; row++,y+=rh) {
        HexRow *hr  = &ctx->rows[row];
        BOOL isHov  = (row==ctx->hoveredRow);
        BOOL isSel  = (row==ctx->selectedRow);

        RECT rowRc = {rc->left,y,rc->right,y+rh};
        FillRect(hdc, &rowRc,
            isSel ? brSel : isHov ? brHov : (row&1) ? brOdd : brEven);

        if (isSel) {
            RECT barRc={rc->left,y,rc->left+3,y+rh};
            FillRect(hdc,&barRc,brAccBar);
        }

        COLORREF rowBgC = isSel  ? RGB(0x00,0x2A,0x44) :
                          isHov  ? RGB(0x18,0x22,0x30) :
                          (row&1)? RGB(0x0F,0x0F,0x15) : RGB(0x0D,0x0D,0x0D);
        SetBkMode (hdc, OPAQUE);
        SetBkColor(hdc, rowBgC);

        SetTextColor(hdc, isSel ? RGB(0x00,0xFF,0xCC) : ctx->clrAddr);
        TextOutA(hdc, xAddr, y+2, hr->addrStr, 16);

        SelectObject(hdc,penVert);
        { int sx=xHex-cw/2; MoveToEx(hdc,sx,y+2,NULL); LineTo(hdc,sx,y+rh-2); }
        SelectObject(hdc,penRow);

        SetTextColor(hdc, isHov ? RGB(0xFF,0xFF,0xFF) : ctx->clrHex);
        TextOutA(hdc, xHex, y+2, hr->hexStr, HEX_BYTES_PER_ROW*3-1);

        SelectObject(hdc,penVert);
        { int sx=xAsc-cw/2; MoveToEx(hdc,sx,y+2,NULL); LineTo(hdc,sx,y+rh-2); }
        SelectObject(hdc,penRow);

        SetTextColor(hdc, isSel ? RGB(0x88,0xFF,0x88) : ctx->clrAscii);
        TextOutA(hdc, xAsc, y+2, hr->asciiStr, HEX_BYTES_PER_ROW);

        MoveToEx(hdc, rc->left, y+rh-1, NULL);
        LineTo  (hdc, rc->right, y+rh-1);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(brEven); DeleteObject(brOdd); DeleteObject(brHov);
    DeleteObject(brSel);  DeleteObject(brAccBar);
    DeleteObject(penRow); DeleteObject(penVert);

    if (ctx->rowCount > ctx->visibleRows) {
        int trkH=H-rh;
        int thH=(int)((float)ctx->visibleRows/ctx->rowCount*trkH);
        if(thH<16)thH=16;
        int ms2=ctx->rowCount-ctx->visibleRows;
        int thY=ms2>0?(int)((float)ctx->scrollRow/ms2*(trkH-thH)):0;
        RECT tr={rc->right-8,rc->top+rh,rc->right,rc->bottom};
        HBRUSH trB=CreateSolidBrush(RGB(0x14,0x14,0x1E)); FillRect(hdc,&tr,trB); DeleteObject(trB);
        RECT th={rc->right-7,rc->top+rh+thY,rc->right-1,rc->top+rh+thY+thH};
        HBRUSH thB=CreateSolidBrush(RGB(0x00,0x55,0x99)); FillRect(hdc,&th,thB); DeleteObject(thB);
        RECT thi={rc->right-7,rc->top+rh+thY,rc->right-5,rc->top+rh+thY+thH};
        HBRUSH hiB=CreateSolidBrush(RGB(0x00,0xAA,0xFF)); FillRect(hdc,&thi,hiB); DeleteObject(hiB);
    }

    SelectObject(hdc, oldFont);
}

BOOL Hex_MouseMove(HexViewCtx *ctx, int mouseY, const RECT *rc)
{
    if (!ctx||!rc||ctx->rowHeight==0) return FALSE;
    int relY=mouseY-rc->top-ctx->rowHeight;
    int row=(relY>=0)?ctx->scrollRow+relY/ctx->rowHeight:-1;
    if(row<0||row>=ctx->rowCount)row=-1;
    BOOL changed=(row!=ctx->hoveredRow);
    ctx->hoveredRow=row; return changed;
}

BOOL Hex_MouseWheel(HexViewCtx *ctx, int delta)
{
    if (!ctx) return FALSE;
    float lines=(float)(-(delta/WHEEL_DELTA)*3);
    ctx->scrollTarget+=lines;
    float ms=(float)(ctx->rowCount-ctx->visibleRows);
    if(ms<0.f)ms=0.f;
    if(ctx->scrollTarget<0.f)ctx->scrollTarget=0.f;
    if(ctx->scrollTarget>ms)ctx->scrollTarget=ms;
    return TRUE;
}

BOOL Hex_AnimateScroll(HexViewCtx *ctx)
{
    if (!ctx) return FALSE;
    float diff=ctx->scrollTarget-ctx->scrollOffset;
    if(diff<0.f)diff=-diff;
    if(diff<0.01f){
        ctx->scrollOffset=ctx->scrollTarget;
        ctx->scrollRow=(int)(ctx->scrollTarget+0.5f); return FALSE;
    }
    ctx->scrollOffset+=(ctx->scrollTarget-ctx->scrollOffset)*0.18f;
    ctx->scrollRow=(int)(ctx->scrollOffset+0.5f); return TRUE;
}

void Hex_RowToText(const HexRow *row, char *dst, SIZE_T dstLen)
{
    if(!row||!dst||dstLen==0) return;
    _snprintf_s(dst,dstLen,_TRUNCATE,"%016IX  %-*s  |%s|",
        row->offset, HEX_BYTES_PER_ROW*3-1, row->hexStr, row->asciiStr);
}

static COLORREF s_bookmarkColors[] = {
    RGB(0xFF,0xDD,0x00), RGB(0xFF,0x66,0x00), RGB(0x00,0xFF,0x88),
    RGB(0xFF,0x44,0xAA), RGB(0x44,0xBB,0xFF)
};

void Hex_AddBookmark(HexViewCtx *ctx, ULONG_PTR addr, const char *label)
{
    if (!ctx) return;
    for (int i = 0; i < ctx->bookmarkCount; i++) {
        if (ctx->bookmarks[i].addr == addr) {
            _snprintf_s(ctx->bookmarks[i].label,
                        sizeof(ctx->bookmarks[i].label),
                        _TRUNCATE, "%s", label ? label : "");
            return;
        }
    }
    int idx = ctx->bookmarkCount < HEX_MAX_BOOKMARKS
              ? ctx->bookmarkCount++
              : HEX_MAX_BOOKMARKS - 1;
    ctx->bookmarks[idx].addr  = addr;
    ctx->bookmarks[idx].color = s_bookmarkColors[idx % 5];
    _snprintf_s(ctx->bookmarks[idx].label,
                sizeof(ctx->bookmarks[idx].label),
                _TRUNCATE, "%s", label ? label : "");
}

void Hex_RemoveBookmark(HexViewCtx *ctx, ULONG_PTR addr)
{
    if (!ctx) return;
    for (int i = 0; i < ctx->bookmarkCount; i++) {
        if (ctx->bookmarks[i].addr == addr) {
            memmove(&ctx->bookmarks[i], &ctx->bookmarks[i+1],
                    (SIZE_T)(ctx->bookmarkCount - i - 1) * sizeof(HexBookmark));
            ctx->bookmarkCount--;
            return;
        }
    }
}

const HexBookmark *Hex_GetBookmark(const HexViewCtx *ctx, ULONG_PTR addr)
{
    if (!ctx) return NULL;
    ULONG_PTR rowBase = (addr / HEX_BYTES_PER_ROW) * HEX_BYTES_PER_ROW;
    for (int i = 0; i < ctx->bookmarkCount; i++) {
        ULONG_PTR bBase = (ctx->bookmarks[i].addr / HEX_BYTES_PER_ROW)
                          * HEX_BYTES_PER_ROW;
        if (bBase == rowBase) return &ctx->bookmarks[i];
    }
    return NULL;
}

BOOL Hex_JumpToAddress(HexViewCtx *ctx, ULONG_PTR addr)
{
    if (!ctx || ctx->rowCount == 0) return FALSE;
    for (int i = 0; i < ctx->rowCount; i++) {
        if (ctx->rows[i].offset <= addr &&
            addr < ctx->rows[i].offset + (ULONG_PTR)ctx->rows[i].byteCount)
        {
            ctx->scrollTarget = (float)(i > 3 ? i - 3 : 0);
            ctx->selectedRow  = i;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL Hex_CopyToClipboard(HexViewCtx *ctx, HWND hWnd)
{
    if (!ctx || ctx->rowCount == 0) return FALSE;
    if (!OpenClipboard(hWnd)) return FALSE;
    EmptyClipboard();

    SIZE_T sz = (SIZE_T)(ctx->rowCount) * 96 + 64;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hMem) { CloseClipboard(); return FALSE; }

    char *buf = (char *)GlobalLock(hMem);
    char *p   = buf;
    char *end = buf + sz - 4;

    for (int i = 0; i < ctx->rowCount && p < end; i++) {
        char line[160];
        Hex_RowToText(&ctx->rows[i], line, sizeof(line));
        int len = (int)strlen(line);
        if (p + len + 2 >= end) break;
        memcpy(p, line, (SIZE_T)len); p += len;
        *p++ = '\r'; *p++ = '\n';
    }
    *p = '\0';

    GlobalUnlock(hMem);
    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    return TRUE;
}
