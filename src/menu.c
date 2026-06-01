// AIO Graphics Test - in-app start menu (Win32).
//
// Two-level menu: MAIN (Run Cube > / GPU Info / Benchmark / Exit) and a CUBE
// submenu (Vulkan / OpenGL / Direct3D 9/11/12 / Back). Returns the chosen
// AioMode. Pure Win32 (BUTTON controls) - no dependencies.
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#include <windows.h>
#include <string.h>

#include "menu.h"

#define ID_FIRST_BUTTON 1000
#define MAX_BUTTONS 8

// Internal (non-AioMode) actions encoded as negative values.
#define ACT_SUBMENU_CUBE (-10)
#define ACT_BACK (-11)

enum { STATE_MAIN, STATE_CUBE };

static int g_state;
static int g_choice;  // resolved AioMode, or -1 while menu is open
static HWND g_header;
static HWND g_buttons[MAX_BUTTONS];
static int g_button_action[MAX_BUTTONS];
static int g_button_count;
static HFONT g_header_font;
static HFONT g_button_font;

static void clear_buttons(void) {
    for (int i = 0; i < g_button_count; i++) {
        if (g_buttons[i]) DestroyWindow(g_buttons[i]);
        g_buttons[i] = NULL;
    }
    g_button_count = 0;
}

static void add_button(HWND parent, HINSTANCE hinst, const char *text, int action, int x, int y, int w,
                       int h) {
    if (g_button_count >= MAX_BUTTONS) return;
    HWND b = CreateWindowA("BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h, parent,
                           (HMENU)(INT_PTR)(ID_FIRST_BUTTON + g_button_count), hinst, NULL);
    if (g_button_font) SendMessage(b, WM_SETFONT, (WPARAM)g_button_font, TRUE);
    g_buttons[g_button_count] = b;
    g_button_action[g_button_count] = action;
    g_button_count++;
}

static void build_menu(HWND hwnd, HINSTANCE hinst) {
    clear_buttons();

    RECT rc;
    GetClientRect(hwnd, &rc);
    int margin = 30;
    int bw = rc.right - 2 * margin;
    int bh = 52;
    int gap = 62;
    int y = 90;

    if (g_state == STATE_MAIN) {
        SetWindowTextA(g_header, "AIO Graphics Test");
        add_button(hwnd, hinst, "Run Cube  >", ACT_SUBMENU_CUBE, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "GPU Info / Report", AIO_MODE_GPUINFO, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "Benchmark", AIO_MODE_BENCH, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "Exit", AIO_MODE_EXIT, margin, y, bw, bh); y += gap;
    } else {
        SetWindowTextA(g_header, "Run Cube  -  pick API");
        add_button(hwnd, hinst, "Cube  -  Vulkan", AIO_MODE_CUBE_VK, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "Cube  -  OpenGL", AIO_MODE_CUBE_GL, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "Cube  -  Direct3D 9", AIO_MODE_CUBE_DX9, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "Cube  -  Direct3D 11", AIO_MODE_CUBE_DX11, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "Cube  -  Direct3D 12", AIO_MODE_CUBE_DX12, margin, y, bw, bh); y += gap;
        add_button(hwnd, hinst, "<  Back", ACT_BACK, margin, y, bw, bh); y += gap;
    }
}

static LRESULT CALLBACK menu_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int idx = id - ID_FIRST_BUTTON;
            if (idx < 0 || idx >= g_button_count) break;
            int action = g_button_action[idx];
            if (action == ACT_SUBMENU_CUBE) {
                g_state = STATE_CUBE;
                build_menu(hwnd, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
            } else if (action == ACT_BACK) {
                g_state = STATE_MAIN;
                build_menu(hwnd, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
            } else {
                g_choice = action;  // a real AioMode
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            g_choice = AIO_MODE_EXIT;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int aio_show_menu(HINSTANCE hInstance) {
    const char *cls = "AIOGraphicsTestMenu";

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = menu_wndproc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);

    g_header_font = CreateFontA(30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_button_font = CreateFontA(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    int win_w = 460;
    int win_h = 620;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;

    HWND hwnd = CreateWindowA(cls, "AIO Graphics Test",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, sx, sy, win_w,
                              win_h, NULL, NULL, hInstance, NULL);
    if (!hwnd) return AIO_MODE_EXIT;

    g_header = CreateWindowA("STATIC", "AIO Graphics Test", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 28,
                             win_w - 56, 40, hwnd, NULL, hInstance, NULL);
    if (g_header_font) SendMessage(g_header, WM_SETFONT, (WPARAM)g_header_font, TRUE);

    g_state = STATE_MAIN;
    g_choice = -1;
    build_menu(hwnd, hInstance);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (IsDialogMessage(hwnd, &msg)) continue;  // tab/enter navigation
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_header_font) { DeleteObject(g_header_font); g_header_font = NULL; }
    if (g_button_font) { DeleteObject(g_button_font); g_button_font = NULL; }
    UnregisterClassA(cls, hInstance);

    return (g_choice < 0) ? AIO_MODE_EXIT : g_choice;
}
