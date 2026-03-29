#ifndef WINVER
#define WINVER       0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "../include/resource.h"
#include "../include/gui.h"
#include "../include/utils.h"
#include "../include/process.h"

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR     lpCmdLine,
                   int       nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC  = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    SetProcessDPIAware();

    AppCtx *ctx = (AppCtx *)HeapAlloc(GetProcessHeap(),
                                       HEAP_ZERO_MEMORY,
                                       sizeof(AppCtx));
    if (!ctx) {
        MessageBoxW(NULL, L"Out of memory.", L"PhantomD", MB_ICONERROR);
        return 1;
    }

    Utils_InitLog(&ctx->log, "phantomd.log");
    Utils_Log(&ctx->log, LOG_INFO, "PhantomD starting up");

    if (Process_EnableDebugPrivilege())
        Utils_Log(&ctx->log, LOG_INFO, "SeDebugPrivilege acquired");
    else
        Utils_Log(&ctx->log, LOG_WARN,
                  "SeDebugPrivilege not available "
                  "(run as Administrator for full access)");

    HWND hWnd = Gui_CreateMainWindow(hInstance, ctx);
    if (!hWnd) {
        MessageBoxW(NULL, L"Failed to create main window.",
                    L"PhantomD", MB_ICONERROR);
        Utils_CloseLog(&ctx->log);
        HeapFree(GetProcessHeap(), 0, ctx);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    Utils_Log(&ctx->log, LOG_INFO, "Window created — entering message loop");

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Utils_Log(&ctx->log, LOG_INFO, "Message loop exited — shutting down");

    HeapFree(GetProcessHeap(), 0, ctx);

    return (int)msg.wParam;
}
