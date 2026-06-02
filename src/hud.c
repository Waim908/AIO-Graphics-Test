// AIO Graphics Test - in-window FPS/API HUD overlay.
//
// A small always-on-top, click-through WS_POPUP bar painted (GDI) over the
// render window's top-left corner. Decoupled from the graphics API's present,
// so the text shows reliably and the same code works for any backend.
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#include <windows.h>
#include <string.h>

#include "hud.h"

static HWND g_hud;
static char g_hud_text[160];
static HFONT g_hud_font;

static LRESULT CALLBACK hud_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        if (g_hud_font) SelectObject(dc, g_hud_font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 255, 128));
        RECT tr = rc;
        tr.left += 8;
        DrawTextA(dc, g_hud_text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_NCHITTEST) return HTTRANSPARENT;  // click-through
    return DefWindowProcA(h, m, w, l);
}

void aio_hud_create(HINSTANCE hinst) {
    static int reg = 0;
    if (!reg) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = hud_proc;
        wc.hInstance = hinst;
        wc.lpszClassName = "AIOHudOverlay";
        RegisterClassA(&wc);
        reg = 1;
    }
    if (!g_hud_font)
        g_hud_font = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_hud = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                            "AIOHudOverlay", "", WS_POPUP, 0, 0, 260, 32, NULL, NULL, hinst, NULL);
}

void aio_hud_update(HWND render_window, const char *text) {
    if (!g_hud) return;
    strncpy(g_hud_text, text, sizeof(g_hud_text) - 1);
    g_hud_text[sizeof(g_hud_text) - 1] = '\0';

    // Size the bar to the text so long labels (e.g. "D3D11 Compute Particles
    // 1122 FPS") aren't clipped. Paint draws at an 8px left margin, so leave
    // 8 left + 16 right padding around the measured text width.
    int w = 260;
    HDC mdc = GetDC(g_hud);
    if (mdc) {
        HFONT old = g_hud_font ? (HFONT)SelectObject(mdc, g_hud_font) : NULL;
        SIZE sz;
        if (GetTextExtentPoint32A(mdc, g_hud_text, (int)strlen(g_hud_text), &sz))
            w = sz.cx + 24;
        if (old) SelectObject(mdc, old);
        ReleaseDC(g_hud, mdc);
    }

    POINT p = {8, 8};
    ClientToScreen(render_window, &p);
    SetWindowPos(g_hud, HWND_TOPMOST, p.x, p.y, w, 32, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hud, NULL, TRUE);
    UpdateWindow(g_hud);
}

void aio_hud_destroy(void) {
    if (g_hud) {
        DestroyWindow(g_hud);
        g_hud = NULL;
    }
    if (g_hud_font) {
        DeleteObject(g_hud_font);
        g_hud_font = NULL;
    }
}
