#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <propidl.h>
#define GDIPVER 0x0110
#include <gdiplus.h>
#include <commctrl.h>
#include <unknwn.h>
#include <commdlg.h>
#include <shellapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../include/gui.h"
#include "../include/resource.h"
#include "../include/process.h"
#include "../include/memory.h"
#include "../include/hexdump.h"
#include "../include/utils.h"

#define FADE_MS          700
#define RIPPLE_SPEED     0.04f
#define SIDEBAR_ROW_H    44
#define MIN_DUMP_BYTES   (256UL  * 1024UL)
#define MAX_DUMP_BYTES   (64UL   * 1024UL * 1024UL)
#define GLOW_PULSE_SPEED 0.03f
static float s_glowPhase = 0.f;
#define SCAN_LINE_ALPHA  12

static const WCHAR CLASS_NAME[] = L"PhantomD_WndClass";

#define CLAMPf(v,lo,hi) ((v)<(lo)?(lo):(v)>(hi)?(hi):(v))
static float LerpF(float a,float b,float t){return a+(b-a)*t;}

static COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    int r=(int)LerpF((float)GetRValue(a),(float)GetRValue(b),t);
    int g=(int)LerpF((float)GetGValue(a),(float)GetGValue(b),t);
    int bv=(int)LerpF((float)GetBValue(a),(float)GetBValue(b),t);
    r=r<0?0:r>255?255:r; g=g<0?0:g>255?255:g; bv=bv<0?0:bv>255?255:bv;
    return RGB(r,g,bv);
}

static void AlphaRect(HDC hdc, int x,int y,int w,int h, COLORREF c, BYTE a) {
    if(w<=0||h<=0||a==0) return;
    HDC m=CreateCompatibleDC(hdc);
    HBITMAP bm=CreateCompatibleBitmap(hdc,w,h);
    HBITMAP ob=(HBITMAP)SelectObject(m,bm);
    HBRUSH br=CreateSolidBrush(c); RECT r={0,0,w,h};
    FillRect(m,&r,br); DeleteObject(br);
    BLENDFUNCTION bf={AC_SRC_OVER,0,a,0};
    AlphaBlend(hdc,x,y,w,h,m,0,0,w,h,bf);
    SelectObject(m,ob); DeleteObject(bm); DeleteDC(m);
}

static void GradientFillV(HDC hdc, int x,int y,int w,int h,
                            COLORREF top, COLORREF bot) {
    if(w<=0||h<=0) return;
    TRIVERTEX tv[2]; GRADIENT_RECT gr={0,1};
    tv[0].x=x;   tv[0].y=y;   tv[0].Red=GetRValue(top)<<8; tv[0].Green=GetGValue(top)<<8; tv[0].Blue=GetBValue(top)<<8; tv[0].Alpha=0xFF00;
    tv[1].x=x+w; tv[1].y=y+h; tv[1].Red=GetRValue(bot)<<8; tv[1].Green=GetGValue(bot)<<8; tv[1].Blue=GetBValue(bot)<<8; tv[1].Alpha=0xFF00;
    GradientFill(hdc,tv,2,&gr,1,GRADIENT_FILL_RECT_V);
}

static void RoundFill(HDC hdc, int x,int y,int w,int h,
                       int r, COLORREF fill, COLORREF border, int bw) {
    HBRUSH br=CreateSolidBrush(fill);
    HPEN   pn=CreatePen(PS_SOLID,bw,border);
    HBRUSH ob=(HBRUSH)SelectObject(hdc,br);
    HPEN   op=(HPEN)  SelectObject(hdc,pn);
    RoundRect(hdc,x,y,x+w,y+h,r*2,r*2);
    SelectObject(hdc,ob); SelectObject(hdc,op);
    DeleteObject(br); DeleteObject(pn);
}

static void DrawGlow(HDC hdc, int cx, int cy, int radius,
                      COLORREF c, BYTE peakAlpha) {
    for(int i=radius; i>0; i-=2) {
        float t = (float)i/radius;
        BYTE a = (BYTE)(peakAlpha * (1.f-t) * (1.f-t));
        AlphaRect(hdc, cx-i, cy-i, i*2, i*2, c, a);
    }
}

static void GlowLine(HDC hdc, int x1, int y, int x2,
                      COLORREF c, BYTE alpha, int thick) {
    for(int i=thick; i>=0; i--) {
        float t = (float)i/thick;
        BYTE a = (BYTE)(alpha * (1.f - t*t*0.7f));
        AlphaRect(hdc, x1, y-i, x2-x1, i*2+1, c, a/4);
    }
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HPEN op = (HPEN)SelectObject(hdc, p);
    MoveToEx(hdc, x1, y, NULL); LineTo(hdc, x2, y);
    SelectObject(hdc, op); DeleteObject(p);
}

static HFONT s_fontUI=NULL, s_fontMono=NULL, s_fontTitle=NULL, s_fontSmall=NULL;

static void InitFonts(void) {
    if(s_fontUI) return;
    s_fontUI    = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,"Segoe UI");
    s_fontSmall = CreateFontA(11,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,"Segoe UI");
    s_fontTitle = CreateFontA(22,0,0,0,FW_BOLD,  0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,"Segoe UI");
    s_fontMono  = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Cascadia Mono");
    if(!s_fontMono)
        s_fontMono = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Consolas");
}

static void DrawTextC(HDC hdc, const char*txt, RECT*rc, UINT fmt, COLORREF clr, HFONT fnt) {
    if(fnt) SelectObject(hdc,fnt);
    SetTextColor(hdc,clr); SetBkMode(hdc,TRANSPARENT);
    DrawTextA(hdc,txt,-1,rc,fmt);
}

static void DrawBackground(AppCtx *ctx, HDC hdc){
    RECT full={0,0,ctx->backW,ctx->backH};
    HBRUSH bg=CreateSolidBrush(CLR_BG); FillRect(hdc,&full,bg); DeleteObject(bg);

    RECT tb={0,0,ctx->backW,TOOLBAR_H};
    HBRUSH tbb=CreateSolidBrush(RGB(0x10,0x10,0x18)); FillRect(hdc,&tb,tbb); DeleteObject(tbb);

    RECT sb={0,ctx->backH-STATUSBAR_H,ctx->backW,ctx->backH};
    HBRUSH sbb=CreateSolidBrush(RGB(0x08,0x08,0x10)); FillRect(hdc,&sb,sbb); DeleteObject(sbb);

    RECT side={0,TOOLBAR_H,SIDEBAR_W-1,ctx->backH-STATUSBAR_H};
    HBRUSH sideb=CreateSolidBrush(CLR_PANEL); FillRect(hdc,&side,sideb); DeleteObject(sideb);

    HPEN lp=CreatePen(PS_SOLID,1,RGB(0x00,0x44,0x66));
    HPEN op=(HPEN)SelectObject(hdc,lp);
    MoveToEx(hdc,0,TOOLBAR_H-1,NULL);            LineTo(hdc,ctx->backW,TOOLBAR_H-1);
    MoveToEx(hdc,SIDEBAR_W-1,TOOLBAR_H,NULL);    LineTo(hdc,SIDEBAR_W-1,ctx->backH-STATUSBAR_H);
    MoveToEx(hdc,0,ctx->backH-STATUSBAR_H,NULL); LineTo(hdc,ctx->backW,ctx->backH-STATUSBAR_H);
    SelectObject(hdc,op); DeleteObject(lp);
}


static void DrawButton(AppCtx*ctx,HDC hdc,ButtonCtx*btn){
    (void)ctx;
    if(!btn->enabled){
        RoundFill(hdc,btn->rc.left,btn->rc.top,
                  btn->rc.right-btn->rc.left,
                  btn->rc.bottom-btn->rc.top,
                  6, RGB(0x10,0x10,0x18),RGB(0x22,0x22,0x30),1);
        RECT r=btn->rc;
        DrawTextC(hdc,btn->label,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE,
                  RGB(0x38,0x38,0x48),s_fontUI);
        return;
    }

    int bw=btn->rc.right-btn->rc.left;
    int bh=btn->rc.bottom-btn->rc.top;
    float h2=btn->hovered?1.f:0.f;

    AlphaRect(hdc,btn->rc.left+2,btn->rc.top+3,bw,bh,RGB(0,0,0),40);

    COLORREF topC=LerpColor(RGB(0x18,0x1A,0x2E),RGB(0x00,0x44,0x80),h2);
    COLORREF botC=LerpColor(RGB(0x10,0x12,0x20),RGB(0x00,0x33,0x66),h2);
    GradientFillV(hdc,btn->rc.left,btn->rc.top,bw,bh,topC,botC);

    COLORREF bclr=LerpColor(RGB(0x30,0x38,0x55),CLR_ACCENT,h2);
    HPEN bp=CreatePen(PS_SOLID,1,bclr);
    HPEN op=(HPEN)SelectObject(hdc,bp);
    SelectObject(hdc,GetStockObject(NULL_BRUSH));
    RoundRect(hdc,btn->rc.left,btn->rc.top,btn->rc.right,btn->rc.bottom,8,8);
    SelectObject(hdc,op); DeleteObject(bp);

    BYTE hlA=(BYTE)(60+h2*80);
    AlphaRect(hdc,btn->rc.left+2,btn->rc.top,bw-4,1,RGB(180,220,255),hlA);

    if(btn->hovered){
        AlphaRect(hdc,btn->rc.left,btn->rc.top,bw,bh,CLR_ACCENT,15);
        GlowLine(hdc, btn->rc.left, btn->rc.top, btn->rc.right, CLR_ACCENT, 120, 3);
    }

    if(btn->rippleRadius>0.f&&btn->rippleAlpha>0.f){
        int rr=(int)btn->rippleRadius;
        HRGN clip=CreateRoundRectRgn(btn->rc.left,btn->rc.top,
                                      btn->rc.right,btn->rc.bottom,8,8);
        SelectClipRgn(hdc,clip);
        AlphaRect(hdc,btn->rippleX-rr,btn->rippleY-rr,rr*2,rr*2,
                   CLR_ACCENT,(BYTE)(btn->rippleAlpha*90));
        SelectClipRgn(hdc,NULL); DeleteObject(clip);
    }

    RECT r=btn->rc;
    COLORREF lclr=btn->hovered?RGB(255,255,255):RGB(0xBB,0xCC,0xEE);
    DrawTextC(hdc,btn->label,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE,lclr,s_fontUI);
}

static void DrawToolbar(AppCtx*ctx,HDC hdc){
    int W=ctx->backW;

    int lx=12,ly=TOOLBAR_H/2;
    float lr=10.f;
    HPEN hp=CreatePen(PS_SOLID,2,CLR_ACCENT);
    HPEN op=(HPEN)SelectObject(hdc,hp);
    SelectObject(hdc,GetStockObject(NULL_BRUSH));
    POINT hex[6];
    for(int i=0;i<6;i++){
        float ang=i*3.14159f/3.f+3.14159f/6.f;
        hex[i].x=(LONG)(lx+lr*cosf(ang));
        hex[i].y=(LONG)(ly+lr*sinf(ang));
    }
    Polygon(hdc,hex,6);
    SelectObject(hdc,op); DeleteObject(hp);
    DrawGlow(hdc,lx,ly,6,CLR_ACCENT,120);

    SelectObject(hdc,s_fontTitle);
    SetBkMode(hdc,TRANSPARENT);
    SetTextColor(hdc,RGB(220,235,255));
    RECT tr={28,0,240,TOOLBAR_H};
    DrawTextA(hdc,"PhantomD",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    SelectObject(hdc,s_fontSmall);
    SetTextColor(hdc,RGB(0,120,180));
    RECT sr={28,TOOLBAR_H/2-1,240,TOOLBAR_H};
    DrawTextA(hdc,"MEMORY HEX DUMPER",-1,&sr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    GlowLine(hdc, 230, 8, 231, RGB(0,100,180), 60, 2);

    if(ctx->proc.hProcess){
        char badge[80];
        _snprintf_s(badge,sizeof(badge),_TRUNCATE,
                    "[*] PID %u   %s",ctx->proc.pid,ctx->proc.name);
        int bx=240,bw=260,by=8,bh=TOOLBAR_H-16;
        GradientFillV(hdc,bx,by,bw,bh,RGB(0,40,20),RGB(0,25,12));
        HPEN gp=CreatePen(PS_SOLID,1,RGB(0,100,50));
        HPEN gop=(HPEN)SelectObject(hdc,gp);
        SelectObject(hdc,GetStockObject(NULL_BRUSH));
        RoundRect(hdc,bx,by,bx+bw,by+bh,6,6);
        SelectObject(hdc,gop); DeleteObject(gp);
        SelectObject(hdc,s_fontUI);
        SetTextColor(hdc,RGB(0,220,100));
        RECT br={bx+8,by,bx+bw,by+bh};
        DrawTextA(hdc,badge,-1,&br,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        float pulse=0.5f+0.5f*sinf(s_glowPhase*3.f);
        DrawGlow(hdc,bx+6,by+bh/2,8,RGB(0,255,100),(BYTE)(100*pulse));
    }

    DrawButton(ctx,hdc,&ctx->btnAttach);
    DrawButton(ctx,hdc,&ctx->btnDetach);
    DrawButton(ctx,hdc,&ctx->btnFilter);
    DrawButton(ctx,hdc,&ctx->btnSearch);
    DrawButton(ctx,hdc,&ctx->btnExport);

    (void)W;
}

static void DrawDropZone(AppCtx*ctx,HDC hdc){
    RECT*dz=&ctx->rcDropZone;
    int x=dz->left,y=dz->top;
    int w=dz->right-dz->left, h=dz->bottom-dz->top;
    if(w<=0||h<=0) return;

    float glow=0.5f+0.5f*sinf(s_glowPhase*1.5f);
    BOOL hov=ctx->dropAnim.isHovered;

    AlphaRect(hdc,x,y,w,h, hov?RGB(0,30,50):RGB(8,10,24), 180);
    GradientFillV(hdc,x,y,w,h/2, RGB(0,20,40),RGB(0,10,20));

    int cs=14;
    COLORREF cc=hov?RGB(0,255,200):LerpColor(RGB(0,80,160),RGB(0,180,255),glow);
    HPEN cp=CreatePen(PS_SOLID,2,cc);
    HPEN cop=(HPEN)SelectObject(hdc,cp);
    MoveToEx(hdc,x,y+cs,NULL);LineTo(hdc,x,y);LineTo(hdc,x+cs,y);
    MoveToEx(hdc,x+w-cs,y,NULL);LineTo(hdc,x+w,y);LineTo(hdc,x+w,y+cs);
    MoveToEx(hdc,x+w,y+h-cs,NULL);LineTo(hdc,x+w,y+h);LineTo(hdc,x+w-cs,y+h);
    MoveToEx(hdc,x+cs,y+h,NULL);LineTo(hdc,x,y+h);LineTo(hdc,x,y+h-cs);
    SelectObject(hdc,cop); DeleteObject(cp);

    BYTE cga=(BYTE)(60*glow+(hov?80:0));
    DrawGlow(hdc,x,   y,   20,cc,cga);
    DrawGlow(hdc,x+w, y,   20,cc,cga);
    DrawGlow(hdc,x+w, y+h, 20,cc,cga);
    DrawGlow(hdc,x,   y+h, 20,cc,cga);

    float dashOffset=ctx->dropAnim.dashOffset;
    int dashLen=14,gapLen=8;
    HPEN dp=CreatePen(PS_SOLID,1,cc);
    HPEN dop=(HPEN)SelectObject(hdc,dp);
    struct{int x0,y0,dx,dy,len;}sides[4]={
        {x,y,1,0,w},{x+w,y,0,1,h},{x+w,y+h,-1,0,w},{x,y+h,0,-1,h}};
    for(int s2=0;s2<4;s2++){
        float pos=-dashOffset;
        while(pos<(float)sides[s2].len){
            float ds=pos<0?0:pos;
            float de=pos+dashLen;
            if(de>(float)sides[s2].len) de=(float)sides[s2].len;
            if(ds<de){
                MoveToEx(hdc,sides[s2].x0+sides[s2].dx*(int)ds,
                              sides[s2].y0+sides[s2].dy*(int)ds,NULL);
                LineTo  (hdc,sides[s2].x0+sides[s2].dx*(int)de,
                              sides[s2].y0+sides[s2].dy*(int)de);
            }
            pos+=dashLen+gapLen;
        }
    }
    SelectObject(hdc,dop); DeleteObject(dp);

    int cx2=x+w/2, cy2=y+h/2-10;
    float ir=18.f*(0.8f+0.2f*glow);
    HPEN ip=CreatePen(PS_SOLID,2,cc);
    HPEN iop=(HPEN)SelectObject(hdc,ip);
    SelectObject(hdc,GetStockObject(NULL_BRUSH));
    POINT ihex[6];
    for(int i=0;i<6;i++){
        float ang=i*3.14159f/3.f+3.14159f/6.f;
        ihex[i].x=(LONG)(cx2+ir*cosf(ang));
        ihex[i].y=(LONG)(cy2+ir*sinf(ang));
    }
    Polygon(hdc,ihex,6);
    DrawGlow(hdc,cx2,cy2,(int)(ir*0.4f),cc,(BYTE)(80*glow));
    SelectObject(hdc,iop); DeleteObject(ip);

    SelectObject(hdc,s_fontUI); SetBkMode(hdc,TRANSPARENT);
    SetTextColor(hdc,LerpColor(RGB(60,90,130),RGB(0,200,255),glow));
    RECT lr2={x,cy2+28,x+w,y+h-8};
    DrawTextA(hdc,hov?"Release to attach":"Drop an .exe to attach  \xB7  or click Attach PID",
              -1,&lr2,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static void DrawSidebar(AppCtx*ctx,HDC hdc){
    RECT*sr=&ctx->rcSidebar;
    int W=SIDEBAR_W, H=sr->bottom-sr->top;

    GradientFillV(hdc,0,sr->top,W,H, RGB(0x0C,0x0C,0x18),RGB(0x08,0x08,0x12));

    GradientFillV(hdc,0,sr->top,W,32, RGB(0x14,0x16,0x28),RGB(0x0C,0x0C,0x1C));
    GlowLine(hdc, 0, sr->top+31, W, CLR_ACCENT, 60, 2);

    SelectObject(hdc,s_fontUI); SetBkMode(hdc,TRANSPARENT);
    SetTextColor(hdc,CLR_ACCENT);
    RECT htr={8,sr->top,W-8,sr->top+32};
    DrawTextA(hdc,"MEMORY REGIONS",-1,&htr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    if(ctx->sideCount>0){
        char cnt[16]; _snprintf_s(cnt,16,_TRUNCATE,"%d",ctx->sideCount);
        SetTextColor(hdc,RGB(0,100,160));
        DrawTextA(hdc,cnt,-1,&htr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }

    if(ctx->sideCount==0){
        SetTextColor(hdc,RGB(40,50,70));
        RECT er={4,sr->top+40,W-4,sr->top+80};
        DrawTextA(hdc,ctx->proc.hProcess?"Scanning...":"No process attached",
                  -1,&er,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        return;
    }

    int yy=sr->top+33;
    int rh=SIDEBAR_ROW_H;

    for(int i=ctx->sideScroll; i<ctx->sideCount&&yy<sr->bottom; i++,yy+=rh){
        SidebarItem*si=&ctx->sideItems[i];
        BOOL sel=si->selected;
        BOOL hov=(ctx->sideHover==i);
        float ha=si->highlightAlpha;

        COLORREF rowBg=(i&1)?RGB(0x0C,0x0C,0x18):RGB(0x0A,0x0A,0x14);
        if(sel) rowBg=LerpColor(rowBg,RGB(0x00,0x20,0x40),ha);
        if(hov&&!sel) rowBg=LerpColor(rowBg,RGB(0x12,0x18,0x30),0.8f);
        HBRUSH rb=CreateSolidBrush(rowBg);
        RECT rr2={0,yy,W-1,yy+rh-1}; FillRect(hdc,&rr2,rb); DeleteObject(rb);

        if(sel){
            BYTE ba=(BYTE)(ha*255);
            AlphaRect(hdc,0,yy,3,rh-1,CLR_ACCENT,ba);
            AlphaRect(hdc,3,yy,W-4,rh-1,CLR_ACCENT,(BYTE)(12*ha));
            GlowLine(hdc, 0, yy, W, CLR_ACCENT, (BYTE)(80*ha), 2);
        }

        const char*typeStr=Utils_MemTypeStr(si->region.type);
        COLORREF tagC=(strcmp(typeStr,"IMG")==0)?RGB(0,160,255):
                       (strcmp(typeStr,"MAP")==0)?RGB(255,160,0):RGB(180,80,255);
        int tagX=6,tagY=yy+4,tagW=28,tagH=14;
        AlphaRect(hdc,tagX,tagY,tagW,tagH,tagC,40);
        HPEN tagP=CreatePen(PS_SOLID,1,tagC);
        HPEN tagOP=(HPEN)SelectObject(hdc,tagP);
        SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,tagX,tagY,tagX+tagW,tagY+tagH);
        SelectObject(hdc,tagOP); DeleteObject(tagP);
        SelectObject(hdc,s_fontSmall);
        SetTextColor(hdc,tagC);
        RECT tr2={tagX,tagY,tagX+tagW,tagY+tagH};
        DrawTextA(hdc,typeStr,-1,&tr2,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        char addr[24];
        _snprintf_s(addr,24,_TRUNCATE,"0x%0*IX",(int)(sizeof(LPVOID)*2),
                    (ULONG_PTR)si->region.baseAddress);
        SelectObject(hdc,s_fontMono);
        SetTextColor(hdc,sel?RGB(0,255,200):LerpColor(RGB(0,100,160),CLR_ACCENT,ha));
        RECT ar={38,yy+3,W-4,yy+rh/2+2};
        DrawTextA(hdc,addr,-1,&ar,DT_LEFT|DT_TOP|DT_SINGLELINE);

        SelectObject(hdc,s_fontSmall);
        SetTextColor(hdc,sel?RGB(180,200,220):RGB(60,80,110));
        RECT lr3={38,yy+rh/2+1,W-4,yy+rh-3};
        DrawTextA(hdc,si->region.label,-1,&lr3,DT_LEFT|DT_TOP|DT_SINGLELINE);

        AlphaRect(hdc,0,yy+rh-1,W,1,RGB(30,40,60),80);
    }

    int visR=(sr->bottom-sr->top-33)/rh;
    if(ctx->sideCount>visR){
        int trkH=sr->bottom-sr->top-33;
        int thH=trkH*visR/ctx->sideCount;
        if(thH<20)thH=20;
        int thY=sr->top+33+trkH*ctx->sideScroll/ctx->sideCount;
        AlphaRect(hdc,W-5,sr->top+33,4,trkH,RGB(0,40,80),100);
        GradientFillV(hdc,W-5,thY,4,thH,RGB(0,120,220),RGB(0,80,180));
    }
}

static void DrawStatusBar(AppCtx*ctx,HDC hdc){
    int W=ctx->backW, H=ctx->backH;
    int top=H-STATUSBAR_H;

    BOOL att=ctx->proc.hProcess!=NULL;
    float pulse=0.6f+0.4f*sinf(s_glowPhase*2.5f);
    COLORREF dotC=att?RGB(0,220,80):RGB(60,60,80);
    if(att) DrawGlow(hdc,10,top+STATUSBAR_H/2,10,RGB(0,255,100),(BYTE)(50*pulse));
    HBRUSH db=CreateSolidBrush(dotC);
    HPEN dp=CreatePen(PS_SOLID,1,dotC);
    HBRUSH dob=(HBRUSH)SelectObject(hdc,db);
    HPEN dop=(HPEN)SelectObject(hdc,dp);
    Ellipse(hdc,5,top+STATUSBAR_H/2-4,15,top+STATUSBAR_H/2+5);
    SelectObject(hdc,dob);SelectObject(hdc,dop);
    DeleteObject(db);DeleteObject(dp);

    SelectObject(hdc,s_fontUI); SetBkMode(hdc,TRANSPARENT);
    SetTextColor(hdc,ctx->statusColor?ctx->statusColor:RGB(100,130,160));
    RECT str2={20,top,W-280,top+STATUSBAR_H};
    DrawTextA(hdc,ctx->statusText[0]?ctx->statusText:"Ready",-1,&str2,
              DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    if(ctx->mem.count>0){
        SIZE_T tot=0;
        for(int i=0;i<ctx->mem.count;i++) tot+=ctx->mem.regions[i].regionSize;
        char info[128];
        _snprintf_s(info,128,_TRUNCATE,"%d regions  \xB7  %zu MB",
                    ctx->mem.count,tot/(1024*1024));
        SetTextColor(hdc,RGB(50,80,110));
        RECT ir={W-320,top,W-8,top+STATUSBAR_H};
        DrawTextA(hdc,info,-1,&ir,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }

    SetTextColor(hdc,RGB(25,35,55));
    RECT vr={W-140,top,W-8,top+STATUSBAR_H};
    DrawTextA(hdc,"PhantomD v1.0",-1,&vr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    SetTextColor(hdc,RGB(0x1A,0x28,0x3A));
    RECT kr={SIDEBAR_W+8,top,W-310,top+STATUSBAR_H};
    DrawTextA(hdc,"Ctrl+G: Jump   Ctrl+C: Copy   I: Inspector   B: Bookmark",
              -1,&kr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
}

void Gui_Paint(AppCtx*ctx,HDC hdc){
    if(!ctx||!ctx->hdcBack) return;
    HDC mdc=ctx->hdcBack;
    InitFonts();
    SelectObject(mdc,s_fontUI);

    DrawBackground(ctx,mdc);

    if(ctx->phase==PHASE_FADE_IN){
        if(ctx->fadeAlpha<1.f){
            SetBkMode(mdc,TRANSPARENT);
            SelectObject(mdc,s_fontTitle);
            SetTextColor(mdc,CLR_ACCENT);
            RECT tr={0,ctx->backH/2-20,ctx->backW,ctx->backH/2+20};
            DrawTextA(mdc,"PhantomD",-1,&tr,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        }
    } else {
        DrawToolbar(ctx,mdc);
        DrawSidebar(ctx,mdc);
        if(!ctx->proc.hProcess) DrawDropZone(ctx,mdc);
        SelectObject(mdc,s_fontMono?s_fontMono:s_fontUI);
        Hex_Paint(&ctx->hexView,mdc,&ctx->rcHexView);
    }
    if (ctx->showInspector && ctx->hexView.rowCount > 0)
        Gui_DrawInspector(ctx, mdc);

    if (ctx->showJumpBar)
        Gui_DrawJumpBar(ctx, mdc);

    DrawStatusBar(ctx,mdc);

    BitBlt(hdc,0,0,ctx->backW,ctx->backH,mdc,0,0,SRCCOPY);
}

void Gui_Animate(AppCtx*ctx){
    if(!ctx) return;

    s_glowPhase += GLOW_PULSE_SPEED;

    if(ctx->phase==PHASE_FADE_IN){
        ctx->fadeAlpha+=1.f/(FADE_MS/16.f);
        if(ctx->fadeAlpha>=1.f){ctx->fadeAlpha=1.f;ctx->phase=PHASE_IDLE;}
    }

    ctx->dropAnim.dashOffset+=0.55f;
    if(ctx->dropAnim.dashOffset>22.f) ctx->dropAnim.dashOffset-=22.f;

    ButtonCtx*btns[]={&ctx->btnAttach,&ctx->btnDetach,&ctx->btnFilter,
                       &ctx->btnSearch,&ctx->btnExport};
    POINT cur; GetCursorPos(&cur); ScreenToClient(ctx->hWnd,&cur);
    int bi;
    for(bi=0;bi<5;bi++){
        btns[bi]->hovered=(btns[bi]->enabled&&PtInRect(&btns[bi]->rc,cur));
        if(btns[bi]->rippleRadius>0.f){
            btns[bi]->rippleRadius+=3.5f;
            btns[bi]->rippleAlpha-=RIPPLE_SPEED;
            if(btns[bi]->rippleAlpha<=0.f){btns[bi]->rippleAlpha=0.f;btns[bi]->rippleRadius=0.f;}
        }
    }

    int si;
    for(si=0;si<ctx->sideCount;si++){
        float tgt=ctx->sideItems[si].selected?1.f:0.f;
        ctx->sideItems[si].highlightAlpha=LerpF(ctx->sideItems[si].highlightAlpha,tgt,0.15f);
    }

    Hex_AnimateScroll(&ctx->hexView);
}


void Gui_ResizeBackBuffer(AppCtx*ctx,int w,int h){
    if(!ctx) return;
    if(ctx->hdcBack&&ctx->backW==w&&ctx->backH==h) return;
    HDC hdc=GetDC(ctx->hWnd);
    if(ctx->hdcBack) DeleteDC(ctx->hdcBack);
    if(ctx->hbmBack) DeleteObject(ctx->hbmBack);
    ctx->hdcBack=CreateCompatibleDC(hdc);
    ctx->hbmBack=CreateCompatibleBitmap(hdc,w,h);
    SelectObject(ctx->hdcBack,ctx->hbmBack);
    ctx->backW=w; ctx->backH=h;
    ReleaseDC(ctx->hWnd,hdc);
}

void Gui_Layout(AppCtx*ctx,int w,int h){
    if(!ctx) return;
    ctx->rcSidebar.left=0;ctx->rcSidebar.top=TOOLBAR_H;ctx->rcSidebar.right=SIDEBAR_W;ctx->rcSidebar.bottom=h-STATUSBAR_H;
    int dzW=w-SIDEBAR_W-40,dzH=DROPZONE_H;
    ctx->rcDropZone.left=SIDEBAR_W+20;ctx->rcDropZone.top=TOOLBAR_H+20;ctx->rcDropZone.right=SIDEBAR_W+20+dzW;ctx->rcDropZone.bottom=TOOLBAR_H+20+dzH;
    ctx->rcHexView.left=SIDEBAR_W;ctx->rcHexView.top=TOOLBAR_H;ctx->rcHexView.right=w;
    ctx->rcHexView.bottom = ctx->showInspector
        ? h - STATUSBAR_H - 110
        : h - STATUSBAR_H;

    int bh=26,by=(TOOLBAR_H-bh)/2,bx=w-8;
    ctx->btnExport.rc.left=bx-88;ctx->btnExport.rc.top=by;ctx->btnExport.rc.right=bx;ctx->btnExport.rc.bottom=by+bh; bx-=94;
    ctx->btnSearch.rc.left=bx-88;ctx->btnSearch.rc.top=by;ctx->btnSearch.rc.right=bx;ctx->btnSearch.rc.bottom=by+bh; bx-=94;
    ctx->btnFilter.rc.left=bx-96;ctx->btnFilter.rc.top=by;ctx->btnFilter.rc.right=bx;ctx->btnFilter.rc.bottom=by+bh; bx-=102;
    ctx->btnDetach.rc.left=bx-78;ctx->btnDetach.rc.top=by;ctx->btnDetach.rc.right=bx;ctx->btnDetach.rc.bottom=by+bh; bx-=84;
    ctx->btnAttach.rc.left=bx-96;ctx->btnAttach.rc.top=by;ctx->btnAttach.rc.right=bx;ctx->btnAttach.rc.bottom=by+bh;
    Gui_ResizeBackBuffer(ctx,w,h);
}

void Gui_SetStatus(AppCtx*ctx,COLORREF color,const char*fmt,...){
    if(!ctx||!fmt) return;
    va_list ap; va_start(ap,fmt);
    _vsnprintf_s(ctx->statusText,sizeof(ctx->statusText),_TRUNCATE,fmt,ap);
    va_end(ap);
    ctx->statusColor=color;
    Utils_Log(&ctx->log,LOG_INFO,"%s",ctx->statusText);
}

void Gui_RippleButton(ButtonCtx*btn,int x,int y){
    if(!btn||!btn->enabled) return;
    btn->rippleRadius=1.f; btn->rippleAlpha=1.f;
    btn->rippleX=x; btn->rippleY=y;
}

void Gui_RefreshRegions(AppCtx*ctx){
    if(!ctx||!ctx->proc.hProcess) return;
    Mem_ScanRegions(&ctx->mem,ctx->proc.hProcess,ctx->filterFlags,&ctx->log);
    int n=ctx->mem.count; if(n>MEM_MAX_REGIONS) n=MEM_MAX_REGIONS;
    ctx->sideCount=n;
    for(int i=0;i<n;i++){
        ctx->sideItems[i].region=ctx->mem.regions[i];
        if(!ctx->sideItems[i].selected) ctx->sideItems[i].highlightAlpha=0.f;
    }
    if(ctx->activeRegion>=n) ctx->activeRegion=-1;
}

void Gui_LoadRegion(AppCtx*ctx,int idx){
    if(!ctx||idx<0||idx>=ctx->sideCount||!ctx->proc.hProcess) return;
    MemRegion*reg=&ctx->sideItems[idx].region;
    SIZE_T rlen=reg->regionSize;
    if(rlen>MAX_DUMP_BYTES) rlen=MAX_DUMP_BYTES;
    if(ctx->dumpBuf){VirtualFree(ctx->dumpBuf,0,MEM_RELEASE);ctx->dumpBuf=NULL;ctx->dumpBufSize=0;}
    ctx->dumpBuf=(BYTE*)VirtualAlloc(NULL,rlen,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ctx->dumpBuf){Gui_SetStatus(ctx,CLR_ERR,"Out of memory");return;}
    SIZE_T got=Mem_Read(ctx->proc.hProcess,reg->baseAddress,ctx->dumpBuf,rlen);
    ctx->dumpBufSize=got;
    if(got==0){
        Gui_SetStatus(ctx,CLR_WARNING,"Read 0 bytes — access denied?");
        VirtualFree(ctx->dumpBuf,0,MEM_RELEASE);ctx->dumpBuf=NULL;ctx->dumpBufSize=0;return;
    }
    HDC hdc=GetDC(ctx->hWnd);
    if(hdc){
        HFONT old=(HFONT)SelectObject(hdc,s_fontMono?s_fontMono:s_fontUI);
        TEXTMETRICA tm; GetTextMetricsA(hdc,&tm);
        ctx->hexView.charWidth=tm.tmAveCharWidth;
        ctx->hexView.rowHeight=tm.tmHeight+4;
        SelectObject(hdc,old); ReleaseDC(ctx->hWnd,hdc);
    }
    int rows=Hex_BuildRows(&ctx->hexView,ctx->dumpBuf,got,(ULONG_PTR)reg->baseAddress);
    for(int i=0;i<ctx->sideCount;i++) ctx->sideItems[i].selected=(i==idx);
    ctx->activeRegion=idx;
    ctx->btnExport.enabled=TRUE;
    Gui_SetStatus(ctx,CLR_SUCCESS,"Loaded %zu KB from 0x%p  (%d rows)",
                  got/1024,reg->baseAddress,rows);
}

typedef struct{int hits;LPVOID first;}SearchUD;
static void SearchMatchCallback(LPVOID addr,void*data){
    SearchUD*ud=(SearchUD*)data; ud->hits++; if(!ud->first) ud->first=addr;
}

DWORD Gui_PromptPid(HWND hParent){
    static const WCHAR CLS[]=L"PhantomD_PidPop";
    HINSTANCE hInst=(HINSTANCE)GetWindowLongPtrW(hParent,GWLP_HINSTANCE);
    WNDCLASSEXW wce; ZeroMemory(&wce,sizeof(wce));
    wce.cbSize=sizeof(wce); wce.lpfnWndProc=DefWindowProcW;
    wce.hInstance=hInst; wce.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wce.lpszClassName=CLS;
    RegisterClassExW(&wce);
    RECT pr; GetWindowRect(hParent,&pr);
    int pw=290,ph=110,px=pr.left+(pr.right-pr.left-pw)/2,py=pr.top+(pr.bottom-pr.top-ph)/2;
    HWND hPop=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,CLS,
        L"Attach to Process \x2014 PhantomD",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        px,py,pw,ph,hParent,NULL,hInst,NULL);
    if(!hPop){UnregisterClassW(CLS,hInst);return 0;}
    CreateWindowExW(0,L"STATIC",L"Enter numeric Process ID (PID):",
        WS_CHILD|WS_VISIBLE|SS_LEFT,10,10,260,18,hPop,NULL,hInst,NULL);
    HWND hEdt=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,
        10,32,260,22,hPop,(HMENU)101,hInst,NULL);
    CreateWindowExW(0,L"BUTTON",L"Attach",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
        pw-186,ph-44,80,26,hPop,(HMENU)IDOK,hInst,NULL);
    CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        pw-98,ph-44,80,26,hPop,(HMENU)IDCANCEL,hInst,NULL);
    ShowWindow(hPop,SW_SHOW); SetFocus(hEdt);
    EnableWindow(hParent,FALSE);
    DWORD pidResult=0; BOOL done=FALSE; MSG m;
    while(!done&&GetMessageW(&m,NULL,0,0)){
        if(m.message==WM_KEYDOWN){
            if(m.wParam==VK_RETURN){WCHAR buf[16]=L"";GetWindowTextW(hEdt,buf,16);pidResult=(DWORD)_wtoi(buf);done=TRUE;break;}
            if(m.wParam==VK_ESCAPE){done=TRUE;break;}
        }
        if(m.message==WM_COMMAND&&m.hwnd==hPop){
            if(LOWORD(m.wParam)==IDOK){WCHAR buf[16]=L"";GetWindowTextW(hEdt,buf,16);pidResult=(DWORD)_wtoi(buf);done=TRUE;break;}
            if(LOWORD(m.wParam)==IDCANCEL){done=TRUE;break;}
        }
        if(m.message==WM_LBUTTONUP){
            POINT pt={(int)(short)LOWORD(m.lParam),(int)(short)HIWORD(m.lParam)};
            ClientToScreen(m.hwnd,&pt); HWND hit=WindowFromPoint(pt);
            if(hit){int id=GetDlgCtrlID(hit);
                if(id==IDOK){WCHAR buf[16]=L"";GetWindowTextW(hEdt,buf,16);pidResult=(DWORD)_wtoi(buf);done=TRUE;}
                else if(id==IDCANCEL){done=TRUE;}}
        }
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    EnableWindow(hParent,TRUE); DestroyWindow(hPop);
    UnregisterClassW(CLS,hInst); SetForegroundWindow(hParent);
    return pidResult;
}

BOOL Gui_PromptSearch(AppCtx*ctx){
    if(!ctx||!ctx->proc.hProcess) return FALSE;
    MessageBoxA(ctx->hWnd,
        "Enter search pattern:\n\n"
        "  Plain text   \x97  searched as ASCII\n"
        "  0x4D5A90     \x97  hex byte pattern\n\n"
        "First match will be highlighted.",
        "Search Memory",MB_OK|MB_ICONINFORMATION);
    if(!ctx->searchPattern[0]){Gui_SetStatus(ctx,CLR_WARNING,"No pattern set");return FALSE;}
    BYTE pat[256]; SIZE_T patLen=0;
    BOOL isHex=(ctx->searchPattern[0]=='0'&&(ctx->searchPattern[1]=='x'||ctx->searchPattern[1]=='X'));
    if(isHex){if(!Utils_HexToBytes(ctx->searchPattern+2,pat,&patLen)){Gui_SetStatus(ctx,CLR_ERR,"Invalid hex");return FALSE;}}
    else{patLen=strlen(ctx->searchPattern);if(!patLen||patLen>sizeof(pat)){Gui_SetStatus(ctx,CLR_ERR,"Pattern too long");return FALSE;}memcpy(pat,ctx->searchPattern,patLen);}
    SearchUD ud; ud.hits=0; ud.first=NULL;
    Gui_SetStatus(ctx,CLR_ACCENT,"Searching...");
    InvalidateRect(ctx->hWnd,NULL,FALSE); UpdateWindow(ctx->hWnd);
    Mem_Search(&ctx->mem,ctx->proc.hProcess,pat,patLen,SearchMatchCallback,&ud,&ctx->log);
    if(!ud.hits){Gui_SetStatus(ctx,CLR_WARNING,"No matches found");return FALSE;}
    Gui_SetStatus(ctx,CLR_SUCCESS,"%d match(es)  \xB7  first at %p",ud.hits,ud.first);
    for(int i=0;i<ctx->mem.count;i++){
        MemRegion*r=&ctx->mem.regions[i];
        if((LPBYTE)ud.first>=(LPBYTE)r->baseAddress&&(LPBYTE)ud.first<(LPBYTE)r->baseAddress+r->regionSize){
            Gui_LoadRegion(ctx,i);
            int trow=(int)((ULONG_PTR)ud.first-(ULONG_PTR)r->baseAddress)/HEX_BYTES_PER_ROW;
            ctx->hexView.selectedRow=trow;
            ctx->hexView.scrollTarget=(float)(trow>5?trow-5:0);
            break;
        }
    }
    return TRUE;
}

void Gui_ExportDump(AppCtx*ctx){
    if(!ctx||ctx->hexView.rowCount==0){Gui_SetStatus(ctx,CLR_WARNING,"No data — select a region first");return;}
    WCHAR path[MAX_PATH]=L"dump.txt";
    OPENFILENAMEW ofn; ZeroMemory(&ofn,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=ctx->hWnd;
    ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=L"Text Files\0*.txt\0All Files\0*.*\0";
    ofn.lpstrDefExt=L"txt"; ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    if(!GetSaveFileNameW(&ofn)) return;
    char pathA[MAX_PATH]; Utils_WideToNarrow(path,pathA,MAX_PATH);
    HANDLE hf=CreateFileA(pathA,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(hf==INVALID_HANDLE_VALUE){char e[256];Utils_FormatLastError(e,256);Gui_SetStatus(ctx,CLR_ERR,"Cannot create: %s",e);return;}
    char hdr[256]; int hn=_snprintf_s(hdr,256,_TRUNCATE,"; PhantomD  PID=%u  Rows=%d\r\n;\r\n",ctx->proc.pid,ctx->hexView.rowCount);
    DWORD wr; WriteFile(hf,hdr,(DWORD)hn,&wr,NULL);
    for(int i=0;i<ctx->hexView.rowCount;i++){
        char line[160]; Hex_RowToText(&ctx->hexView.rows[i],line,sizeof(line));
        int ll=(int)strlen(line); line[ll]='\r';line[ll+1]='\n';line[ll+2]=0;
        WriteFile(hf,line,(DWORD)(ll+2),&wr,NULL);
    }
    CloseHandle(hf);
    Gui_SetStatus(ctx,CLR_SUCCESS,"Exported %d rows to %s",ctx->hexView.rowCount,pathA);
}

void Gui_HandleDrop(AppCtx*ctx,const char*path){
    if(!ctx||!path||!path[0]) return;
    SIZE_T pl=strlen(path);
    if(pl<4||_stricmp(path+pl-4,".exe")!=0){Gui_SetStatus(ctx,CLR_WARNING,"Drop an .exe file");return;}
    DWORD pid=Process_ResolveExeToPid(path);
    if(pid){
        if(ctx->proc.hProcess) Process_Detach(&ctx->proc,&ctx->log);
        if(ctx->dumpBuf){VirtualFree(ctx->dumpBuf,0,MEM_RELEASE);ctx->dumpBuf=NULL;ctx->dumpBufSize=0;}
        if(!Process_Attach(&ctx->proc,pid,&ctx->log)){Gui_SetStatus(ctx,CLR_ERR,"Attach failed");return;}
        ctx->phase=PHASE_VIEWING; ctx->sideCount=0; ctx->activeRegion=-1;
        ctx->btnDetach.enabled=TRUE; ctx->btnSearch.enabled=TRUE; ctx->btnFilter.enabled=TRUE;
        Gui_RefreshRegions(ctx);
        Gui_SetStatus(ctx,CLR_SUCCESS,"Attached to PID %u  (%s)",pid,ctx->proc.name);
    } else {
        const char*base=strrchr(path,'\\'); base=base?base+1:path;
        char msg[MAX_PATH+64]; _snprintf_s(msg,sizeof(msg),_TRUNCATE,"'%s' not running.\n\nLaunch it?",base);
        if(MessageBoxA(ctx->hWnd,msg,"PhantomD",MB_YESNO|MB_ICONQUESTION)==IDYES){
            STARTUPINFOA si; ZeroMemory(&si,sizeof(si)); si.cb=sizeof(si);
            PROCESS_INFORMATION pi; ZeroMemory(&pi,sizeof(pi));
            if(CreateProcessA(path,NULL,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)){
                Sleep(600); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                if(ctx->proc.hProcess) Process_Detach(&ctx->proc,&ctx->log);
                if(Process_Attach(&ctx->proc,pi.dwProcessId,&ctx->log)){
                    ctx->phase=PHASE_VIEWING; ctx->sideCount=0; ctx->activeRegion=-1;
                    ctx->btnDetach.enabled=TRUE; ctx->btnSearch.enabled=TRUE; ctx->btnFilter.enabled=TRUE;
                    Gui_RefreshRegions(ctx);
                    Gui_SetStatus(ctx,CLR_SUCCESS,"Launched & attached PID %u",pi.dwProcessId);
                }
            }
        }
    }
}

static ButtonCtx*HitBtn(AppCtx*ctx,int x,int y){
    POINT pt={x,y};
    ButtonCtx*btns[]={&ctx->btnAttach,&ctx->btnDetach,&ctx->btnFilter,&ctx->btnSearch,&ctx->btnExport};
    for(int i=0;i<5;i++) if(btns[i]->enabled&&PtInRect(&btns[i]->rc,pt)) return btns[i];
    return NULL;
}

LRESULT CALLBACK Gui_WndProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam){
    AppCtx*ctx=(AppCtx*)GetWindowLongPtrW(hWnd,GWLP_USERDATA);
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT ps;HDC hdc=BeginPaint(hWnd,&ps);
        if(ctx)Gui_Paint(ctx,hdc);else{RECT r;GetClientRect(hWnd,&r);HBRUSH b=CreateSolidBrush(CLR_BG);FillRect(hdc,&r,b);DeleteObject(b);}
        EndPaint(hWnd,&ps);return 0;}
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:{int w=LOWORD(lParam),h=HIWORD(lParam);if(ctx&&w>0&&h>0){Gui_Layout(ctx,w,h);InvalidateRect(hWnd,NULL,FALSE);}return 0;}
    case WM_GETMINMAXINFO:{MINMAXINFO*mm=(MINMAXINFO*)lParam;mm->ptMinTrackSize.x=860;mm->ptMinTrackSize.y=560;return 0;}
    case WM_TIMER:
        if(!ctx) return 0;
        if(wParam==TIMER_ANIM){Gui_Animate(ctx);InvalidateRect(hWnd,NULL,FALSE);}
        else if(wParam==TIMER_REFRESH) Gui_RefreshRegions(ctx);
        return 0;
    case WM_MOUSEMOVE:{if(!ctx)return 0;int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
        POINT pt={x,y};
        ButtonCtx*btns[]={&ctx->btnAttach,&ctx->btnDetach,&ctx->btnFilter,&ctx->btnSearch,&ctx->btnExport};
        for(int i=0;i<5;i++) btns[i]->hovered=(btns[i]->enabled&&PtInRect(&btns[i]->rc,pt));
        ctx->sideHover=-1;
        if(x<SIDEBAR_W&&y>TOOLBAR_H+33){int ri=ctx->sideScroll+(y-TOOLBAR_H-33)/SIDEBAR_ROW_H;if(ri>=0&&ri<ctx->sideCount)ctx->sideHover=ri;}
        if(x>=SIDEBAR_W) Hex_MouseMove(&ctx->hexView,y,&ctx->rcHexView);
        return 0;}
    case WM_MOUSEWHEEL:{if(!ctx)return 0;int delta=GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt;GetCursorPos(&pt);ScreenToClient(hWnd,&pt);
        if(pt.x<SIDEBAR_W){ctx->sideScroll+=-(delta/WHEEL_DELTA)*3;if(ctx->sideScroll<0)ctx->sideScroll=0;
            int maxS=ctx->sideCount-((ctx->rcSidebar.bottom-ctx->rcSidebar.top-33)/SIDEBAR_ROW_H);
            if(maxS<0)maxS=0;
            if(ctx->sideScroll>maxS)ctx->sideScroll=maxS;}
        else Hex_MouseWheel(&ctx->hexView,delta);
        return 0;}
    case WM_LBUTTONDOWN:{if(!ctx)return 0;SetCapture(hWnd);
        int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
        ButtonCtx*hit=HitBtn(ctx,x,y);
        if(hit){Gui_RippleButton(hit,x,y);
            if(hit==&ctx->btnAttach){DWORD pid=Gui_PromptPid(hWnd);if(pid>0){
                if(ctx->proc.hProcess)Process_Detach(&ctx->proc,&ctx->log);
                if(ctx->dumpBuf){VirtualFree(ctx->dumpBuf,0,MEM_RELEASE);ctx->dumpBuf=NULL;ctx->dumpBufSize=0;}
                if(Process_Attach(&ctx->proc,pid,&ctx->log)){ctx->phase=PHASE_VIEWING;ctx->sideCount=0;ctx->activeRegion=-1;
                    ctx->btnDetach.enabled=TRUE;ctx->btnSearch.enabled=TRUE;ctx->btnFilter.enabled=TRUE;
                    Gui_RefreshRegions(ctx);}else Gui_SetStatus(ctx,CLR_ERR,"Cannot attach to PID %u",pid);}}
            else if(hit==&ctx->btnDetach){if(ctx->proc.hProcess)Process_Detach(&ctx->proc,&ctx->log);
                if(ctx->dumpBuf){VirtualFree(ctx->dumpBuf,0,MEM_RELEASE);ctx->dumpBuf=NULL;ctx->dumpBufSize=0;}
                ctx->sideCount=0;ctx->activeRegion=-1;ctx->hexView.rowCount=0;ctx->phase=PHASE_IDLE;
                ctx->btnDetach.enabled=FALSE;ctx->btnSearch.enabled=FALSE;ctx->btnFilter.enabled=FALSE;ctx->btnExport.enabled=FALSE;
                Gui_SetStatus(ctx,CLR_SUBTEXT,"Detached");}
            else if(hit==&ctx->btnFilter){
                if(ctx->filterFlags==MEMFILT_ALL)ctx->filterFlags=MEMFILT_PRIVATE;
                else if(ctx->filterFlags==MEMFILT_PRIVATE)ctx->filterFlags=MEMFILT_IMAGE;
                else if(ctx->filterFlags==MEMFILT_IMAGE)ctx->filterFlags=MEMFILT_MAPPED;
                else ctx->filterFlags=MEMFILT_ALL;
                const char*fn=(ctx->filterFlags==MEMFILT_ALL)?"All":(ctx->filterFlags==MEMFILT_PRIVATE)?"Private":(ctx->filterFlags==MEMFILT_IMAGE)?"Image":"Mapped";
                _snprintf_s(ctx->btnFilter.label,sizeof(ctx->btnFilter.label),_TRUNCATE,"Filter: %s",fn);
                Gui_RefreshRegions(ctx);}
            else if(hit==&ctx->btnSearch) Gui_PromptSearch(ctx);
            else if(hit==&ctx->btnExport) Gui_ExportDump(ctx);
            return 0;}
        if(x<SIDEBAR_W&&y>TOOLBAR_H+33){int ri=ctx->sideScroll+(y-TOOLBAR_H-33)/SIDEBAR_ROW_H;
            if(ri>=0&&ri<ctx->sideCount){Gui_LoadRegion(ctx,ri);ctx->btnExport.enabled=TRUE;}}
        return 0;}
    case WM_LBUTTONUP: ReleaseCapture(); return 0;
    case WM_KEYDOWN:if(!ctx)return 0;
        switch(wParam){
        case VK_ESCAPE:if(ctx->proc.hProcess){Process_Detach(&ctx->proc,&ctx->log);ctx->sideCount=0;ctx->activeRegion=-1;ctx->hexView.rowCount=0;ctx->phase=PHASE_IDLE;ctx->btnDetach.enabled=FALSE;ctx->btnSearch.enabled=FALSE;ctx->btnExport.enabled=FALSE;Gui_SetStatus(ctx,CLR_SUBTEXT,"Detached");}break;
        case VK_F5: Gui_RefreshRegions(ctx); break;
        case 'C':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (Hex_CopyToClipboard(&ctx->hexView, hWnd))
                    Gui_SetStatus(ctx, CLR_SUCCESS, "Copied %d rows to clipboard",
                                  ctx->hexView.rowCount);
            }
            break;
        case 'G':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                ctx->showJumpBar = !ctx->showJumpBar;
                ctx->jumpLen = 0;
                ctx->jumpBuf[0] = '\0';
            }
            break;
        case 'I':
            ctx->showInspector = !ctx->showInspector;
            Gui_Layout(ctx, ctx->backW, ctx->backH);
            break;
        case 'B':
            if (ctx->hexView.selectedRow >= 0 &&
                ctx->hexView.selectedRow < ctx->hexView.rowCount) {
                ULONG_PTR addr = ctx->hexView.rows[ctx->hexView.selectedRow].offset;
                char lbl[32];
                _snprintf_s(lbl, sizeof(lbl), _TRUNCATE, "0x%IX", addr);
                Hex_AddBookmark(&ctx->hexView, addr, lbl);
                Gui_SetStatus(ctx, CLR_SUCCESS, "Bookmark added at 0x%IX", addr);
            }
            break;
        case VK_UP:Hex_MouseWheel(&ctx->hexView,WHEEL_DELTA);break;
        case VK_DOWN:Hex_MouseWheel(&ctx->hexView,-WHEEL_DELTA);break;
        case VK_PRIOR:ctx->hexView.scrollTarget-=(float)ctx->hexView.visibleRows;if(ctx->hexView.scrollTarget<0)ctx->hexView.scrollTarget=0;break;
        case VK_NEXT:ctx->hexView.scrollTarget+=(float)ctx->hexView.visibleRows;break;}
        return 0;
    case WM_CHAR:
        if (ctx && ctx->showJumpBar) Gui_OnChar(ctx, (WCHAR)wParam);
        return 0;
    case WM_DROPFILES:{if(!ctx){DragFinish((HDROP)wParam);return 0;}
        WCHAR wp[MAX_PATH];ctx->dropAnim.isHovered=FALSE;
        if(DragQueryFileW((HDROP)wParam,0,wp,MAX_PATH)){char p[MAX_PATH];Utils_WideToNarrow(wp,p,MAX_PATH);Gui_HandleDrop(ctx,p);}
        DragFinish((HDROP)wParam);return 0;}
    case WM_DESTROY:if(ctx){KillTimer(hWnd,TIMER_ANIM);KillTimer(hWnd,TIMER_REFRESH);Gui_Destroy(ctx);}PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hWnd,msg,wParam,lParam);
}

HWND Gui_CreateMainWindow(HINSTANCE hInst,AppCtx*ctx){
    if(!ctx) return NULL;
    GdiplusStartupInput gsi={1,NULL,FALSE,FALSE};
    GdiplusStartup(&ctx->gdiplusToken,&gsi,NULL);
    InitFonts();

    WNDCLASSEXW wc; ZeroMemory(&wc,sizeof(wc));
    wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=Gui_WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName=CLASS_NAME;
    wc.hIcon  =LoadIconW(hInst,MAKEINTRESOURCEW(1));
    wc.hIconSm=LoadIconW(hInst,MAKEINTRESOURCEW(1));
    if(!wc.hIcon)  wc.hIcon  =LoadIcon(NULL,IDI_APPLICATION);
    if(!wc.hIconSm)wc.hIconSm=LoadIcon(NULL,IDI_APPLICATION);
    RegisterClassExW(&wc);

    int sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);
    int ww=1280,wh=820,wx=(sw-ww)/2,wy=(sh-wh)/2;
    HWND hWnd=CreateWindowExW(WS_EX_ACCEPTFILES,CLASS_NAME,
        L"PhantomD \x2014 Memory Hex Dumper",WS_OVERLAPPEDWINDOW,
        wx,wy,ww,wh,NULL,NULL,hInst,NULL);
    if(!hWnd) return NULL;

    SetWindowLongPtrW(hWnd,GWLP_USERDATA,(LONG_PTR)ctx);
    ctx->hWnd=hWnd; ctx->hInst=hInst;

    HICON hIco=LoadIconW(hInst,MAKEINTRESOURCEW(1));
    if(hIco){SendMessageW(hWnd,WM_SETICON,ICON_BIG,(LPARAM)hIco);SendMessageW(hWnd,WM_SETICON,ICON_SMALL,(LPARAM)hIco);}

    _snprintf_s(ctx->btnAttach.label,sizeof(ctx->btnAttach.label),_TRUNCATE,"Attach PID");
    _snprintf_s(ctx->btnDetach.label,sizeof(ctx->btnDetach.label),_TRUNCATE,"Detach");
    _snprintf_s(ctx->btnFilter.label,sizeof(ctx->btnFilter.label),_TRUNCATE,"Filter: All");
    _snprintf_s(ctx->btnSearch.label,sizeof(ctx->btnSearch.label),_TRUNCATE,"Search");
    _snprintf_s(ctx->btnExport.label,sizeof(ctx->btnExport.label),_TRUNCATE,"Export");
    ctx->btnAttach.enabled=TRUE;
    ctx->filterFlags=MEMFILT_ALL; ctx->activeRegion=-1; ctx->sideHover=-1;
    ctx->dropAnim.glowUp=TRUE; ctx->phase=PHASE_FADE_IN; ctx->fadeAlpha=0.f;
    ctx->showInspector=FALSE; ctx->showJumpBar=FALSE; ctx->showBookmarks=FALSE;

    RECT rc; GetClientRect(hWnd,&rc);
    Gui_Layout(ctx,rc.right,rc.bottom);

    HDC hdc=GetDC(hWnd);
    Hex_InitView(&ctx->hexView,hdc,18);
    if(s_fontMono){HFONT old=(HFONT)SelectObject(hdc,s_fontMono);TEXTMETRICA tm;GetTextMetricsA(hdc,&tm);ctx->hexView.charWidth=tm.tmAveCharWidth;ctx->hexView.rowHeight=tm.tmHeight+4;SelectObject(hdc,old);}
    ReleaseDC(hWnd,hdc);

    Gui_SetStatus(ctx,CLR_SUBTEXT,"Ready  \x97  drop an .exe or click Attach PID");
    SetTimer(hWnd,TIMER_ANIM,16,NULL);
    SetTimer(hWnd,TIMER_REFRESH,1000,NULL);
    DragAcceptFiles(hWnd,TRUE);
    return hWnd;
}

void Gui_Destroy(AppCtx*ctx){
    if(!ctx) return;
    if(ctx->proc.hProcess) Process_Detach(&ctx->proc,&ctx->log);
    if(ctx->dumpBuf){VirtualFree(ctx->dumpBuf,0,MEM_RELEASE);ctx->dumpBuf=NULL;}
    Hex_DestroyView(&ctx->hexView);
    if(s_fontUI)   {DeleteObject(s_fontUI);   s_fontUI=NULL;}
    if(s_fontMono) {DeleteObject(s_fontMono); s_fontMono=NULL;}
    if(s_fontTitle){DeleteObject(s_fontTitle);s_fontTitle=NULL;}
    if(s_fontSmall){DeleteObject(s_fontSmall);s_fontSmall=NULL;}
    if(ctx->hdcBack) DeleteDC(ctx->hdcBack);
    if(ctx->hbmBack) DeleteObject(ctx->hbmBack);
    Utils_CloseLog(&ctx->log);
    GdiplusShutdown(ctx->gdiplusToken);
}

void Gui_DrawInspector(AppCtx *ctx, HDC hdc)
{
    if (!ctx) return;
    int W  = ctx->backW;
    int H  = ctx->backH;
    int top = H - STATUSBAR_H - 110;
    int h2  = 110;

    RECT rc = {SIDEBAR_W, top, W, top + h2};
    HBRUSH bg = CreateSolidBrush(RGB(0x0A, 0x0C, 0x18));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN bp = CreatePen(PS_SOLID, 1, RGB(0x00, 0x55, 0x88));
    HPEN op = (HPEN)SelectObject(hdc, bp);
    MoveToEx(hdc, SIDEBAR_W, top, NULL);
    LineTo  (hdc, W, top);
    SelectObject(hdc, op);
    DeleteObject(bp);

    if (ctx->hexView.selectedRow >= 0 &&
        ctx->hexView.selectedRow < ctx->hexView.rowCount &&
        ctx->dumpBuf)
    {
        HexRow *hr  = &ctx->hexView.rows[ctx->hexView.selectedRow];
        ULONG_PTR off = hr->offset -
            (ULONG_PTR)ctx->sideItems[ctx->activeRegion].region.baseAddress;
        int avail = (int)(ctx->dumpBufSize - off);
        if (avail > 8) avail = 8;
        if (avail > 0)
            Mem_FillInspector(&ctx->inspector,
                               ctx->dumpBuf + off, avail, hr->offset);
    }

    ByteInspector *bi = &ctx->inspector;
    if (bi->count == 0) return;

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, s_fontSmall);
    SetTextColor(hdc, RGB(0x00, 0xAA, 0xFF));
    RECT hdr = {SIDEBAR_W + 8, top + 4, W - 8, top + 18};
    char addrLine[64];
    _snprintf_s(addrLine, sizeof(addrLine), _TRUNCATE,
                "INSPECTOR   @ 0x%IX", bi->addr);
    DrawTextA(hdc, addrLine, -1, &hdr, DT_LEFT | DT_SINGLELINE);

    int col1 = SIDEBAR_W + 10;
    int col2 = SIDEBAR_W + (W - SIDEBAR_W) / 2;
    int y    = top + 22;
    int lh   = 14;

    SelectObject(hdc, s_fontMono ? s_fontMono : s_fontSmall);

    struct { const char *label; char value[48]; } fields[] = {
        {"INT8  ", ""}, {"UINT8 ", ""}, {"INT16 ", ""}, {"UINT16", ""},
        {"INT32 ", ""}, {"UINT32", ""}, {"INT64 ", ""}, {"UINT64", ""},
        {"FLOAT ", ""}, {"DOUBLE", ""}, {"ASCII ", ""},
    };
    _snprintf_s(fields[0].value,  48, _TRUNCATE, "%d",   (int)bi->i8);
    _snprintf_s(fields[1].value,  48, _TRUNCATE, "%u",   (unsigned)bi->u8);
    _snprintf_s(fields[2].value,  48, _TRUNCATE, "%d",   (int)bi->i16);
    _snprintf_s(fields[3].value,  48, _TRUNCATE, "%u",   (unsigned)bi->u16);
    _snprintf_s(fields[4].value,  48, _TRUNCATE, "%ld",  (long)bi->i32);
    _snprintf_s(fields[5].value,  48, _TRUNCATE, "%lu",  (unsigned long)bi->u32);
    _snprintf_s(fields[6].value,  48, _TRUNCATE, "%I64d",(long long)bi->i64);
    _snprintf_s(fields[7].value,  48, _TRUNCATE, "%I64u",(unsigned long long)bi->u64);
    _snprintf_s(fields[8].value,  48, _TRUNCATE, "%.6g", (double)bi->f32);
    _snprintf_s(fields[9].value,  48, _TRUNCATE, "%.10g", bi->f64);
    _snprintf_s(fields[10].value, 48, _TRUNCATE, "%s",   bi->asci);

    for (int i = 0; i < 11; i++) {
        int cx = (i < 6) ? col1 : col2;
        int cy = y + (i < 6 ? i : i - 6) * lh;
        char line[80];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "%-7s %s", fields[i].label, fields[i].value);
        COLORREF vc = (i % 2 == 0) ? RGB(0xCC, 0xCC, 0xCC) : RGB(0x99, 0xAA, 0xBB);
        SetTextColor(hdc, vc);
        RECT lr = {cx, cy, cx + (W - SIDEBAR_W) / 2 - 8, cy + lh};
        SetBkColor(hdc, RGB(0x0A, 0x0C, 0x18));
        SetBkMode(hdc, OPAQUE);
        DrawTextA(hdc, line, -1, &lr, DT_LEFT | DT_SINGLELINE);
    }
}

void Gui_DrawJumpBar(AppCtx *ctx, HDC hdc)
{
    if (!ctx) return;
    int W  = ctx->backW;
    int H  = ctx->backH;

    int bw = 400, bh = 60;
    int bx = (W - bw) / 2;
    int by = (H - bh) / 2;

    AlphaRect(hdc, 0, 0, W, H, RGB(0,0,0), 120);

    RoundFill(hdc, bx, by, bw, bh, 8,
              RGB(0x10, 0x12, 0x22),
              RGB(0x00, 0xAA, 0xFF), 1);

    AlphaRect(hdc, bx + 2, by, bw - 4, 2, CLR_ACCENT, 100);

    SelectObject(hdc, s_fontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0x00, 0xAA, 0xFF));
    RECT lr = {bx + 12, by + 6, bx + bw - 12, by + 24};
    DrawTextA(hdc, "Jump to Address  (Ctrl+G to cancel, Enter to go)",
              -1, &lr, DT_LEFT | DT_SINGLELINE);

    RECT ir = {bx + 10, by + 26, bx + bw - 10, by + bh - 8};
    RoundFill(hdc, ir.left, ir.top,
              ir.right - ir.left, ir.bottom - ir.top,
              4, RGB(0x06, 0x08, 0x14), RGB(0x00, 0x77, 0xCC), 1);

    char display[48];
    _snprintf_s(display, sizeof(display), _TRUNCATE,
                "0x%s_", ctx->jumpBuf);
    SelectObject(hdc, s_fontMono ? s_fontMono : s_fontUI);
    SetTextColor(hdc, RGB(0x00, 0xFF, 0xCC));
    SetBkMode(hdc, TRANSPARENT);
    RECT tr = {ir.left + 8, ir.top, ir.right - 4, ir.bottom};
    DrawTextA(hdc, display, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void Gui_OnChar(AppCtx *ctx, WCHAR ch)
{
    if (!ctx || !ctx->showJumpBar) return;

    if (ch == 27) {
        ctx->showJumpBar = FALSE;
        ctx->jumpLen = 0;
        ctx->jumpBuf[0] = '\0';
        return;
    }

    if (ch == 13) {
        ULONG_PTR addr = 0;
        if (Mem_ParseAddress(ctx->jumpBuf, &addr)) {
            int ri = Mem_FindRegion(&ctx->mem, addr);
            if (ri >= 0) {
                Gui_LoadRegion(ctx, ri);
                Hex_JumpToAddress(&ctx->hexView, addr);
                Gui_SetStatus(ctx, CLR_SUCCESS,
                              "Jumped to 0x%IX", addr);
            } else {
                Gui_SetStatus(ctx, CLR_WARNING,
                              "Address 0x%IX not in any readable region", addr);
            }
        } else {
            Gui_SetStatus(ctx, CLR_ERR, "Invalid address: '%s'", ctx->jumpBuf);
        }
        ctx->showJumpBar = FALSE;
        ctx->jumpLen = 0;
        ctx->jumpBuf[0] = '\0';
        return;
    }

    if (ch == 8) {
        if (ctx->jumpLen > 0)
            ctx->jumpBuf[--ctx->jumpLen] = '\0';
        return;
    }

    char c = (char)(ch & 0xFF);
    BOOL valid = (c >= '0' && c <= '9') ||
                 (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F') ||
                 (c == 'x' || c == 'X');
    if (valid && ctx->jumpLen < 20) {
        ctx->jumpBuf[ctx->jumpLen++] = c;
        ctx->jumpBuf[ctx->jumpLen]   = '\0';
    }
}
