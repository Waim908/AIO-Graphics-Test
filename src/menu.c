// AIO Graphics Test - app shell (Win32).
//
// A persistent left sidebar (the menu, always visible) + a content pane on the
// right. GPU Info opens IN-FRAME as a tabbed Vulkan/OpenGL view (like the
// standalone GPUInfo.exe). Cube tests open in a NEW window (a separate process,
// so the menu stays usable and you can switch between tests).
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "gpuinfo.h"

#define SB_W 150
#define HEADER_H 26
#define ID_FIRST_BUTTON 1000
#define ID_TAB 2000

typedef struct {
    const char *label;
    int action;  // AioMode
} SbItem;

static SbItem g_items[] = {
    {"Cube  -  Vulkan", AIO_MODE_CUBE_VK},     {"Cube  -  OpenGL", AIO_MODE_CUBE_GL},
    {"Cube  -  Direct3D 9", AIO_MODE_CUBE_DX9}, {"Cube  -  Direct3D 11", AIO_MODE_CUBE_DX11},
    {"Cube  -  Direct3D 12", AIO_MODE_CUBE_DX12}, {"GPU Info", AIO_MODE_GPUINFO},
    {"Benchmark", AIO_MODE_BENCH},             {"Exit", AIO_MODE_EXIT},
};
#define NITEMS ((int)(sizeof(g_items) / sizeof(g_items[0])))

static HINSTANCE g_hinst;
static HWND g_header;
static HWND g_sidebar[NITEMS];
static HFONT g_ui_font;
static HFONT g_header_font;
static HFONT g_mono_font;

// Content views.
static HWND g_tab;       // GPU Info tab control
static HWND g_edit_vk;   // GPU Info: Vulkan tab text
static HWND g_edit_gl;   // GPU Info: OpenGL tab text
static HWND g_placeholder;

#define ID_CB_FIRST 3000  // content-area buttons (Benchmark + scene-picker views)
#define MAX_CB 8
static HWND g_cbtn[MAX_CB];
static HWND g_cbtn_result[MAX_CB];   // result label next to each benchmark button
static const char *g_cbtn_arg[MAX_CB];
static const char *g_cbtn_label[MAX_CB];  // API label for the result file name
static HANDLE g_cbtn_proc[MAX_CB];   // running benchmark process (polled)
static int g_cbtn_n;
static int g_cb_bench;  // 1 = Benchmark view (poll + show result); 0 = launch-only

static void get_content_rect(HWND frame, RECT *out) {
    RECT rc;
    GetClientRect(frame, &rc);
    out->left = SB_W + 10;
    out->top = 10 + HEADER_H;
    out->right = rc.right - 10;
    out->bottom = rc.bottom - 10;
}

static void destroy_content(void) {
    if (g_edit_vk) { DestroyWindow(g_edit_vk); g_edit_vk = NULL; }
    if (g_edit_gl) { DestroyWindow(g_edit_gl); g_edit_gl = NULL; }
    if (g_tab) { DestroyWindow(g_tab); g_tab = NULL; }
    if (g_placeholder) { DestroyWindow(g_placeholder); g_placeholder = NULL; }
    for (int i = 0; i < g_cbtn_n; i++) {
        if (g_cbtn[i]) DestroyWindow(g_cbtn[i]);
        g_cbtn[i] = NULL;
        if (g_cbtn_result[i]) DestroyWindow(g_cbtn_result[i]);
        g_cbtn_result[i] = NULL;
        if (g_cbtn_proc[i]) {
            CloseHandle(g_cbtn_proc[i]);
            g_cbtn_proc[i] = NULL;
        }
    }
    g_cbtn_n = 0;
}

static HWND make_report_edit(HWND frame, const RECT *r, const char *text) {
    HWND e = CreateWindowA("EDIT", "",
                           WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                           r->left, r->top, r->right - r->left, r->bottom - r->top, frame, NULL,
                           g_hinst, NULL);
    SendMessage(e, EM_SETLIMITTEXT, 0, 0);
    if (g_mono_font) SendMessage(e, WM_SETFONT, (WPARAM)g_mono_font, TRUE);
    if (text) SetWindowTextA(e, text);
    return e;
}

static void show_gpuinfo(HWND frame) {
    destroy_content();
    SetWindowTextA(g_header, "GPU Info  (Vulkan / OpenGL)");

    RECT cr;
    get_content_rect(frame, &cr);

    g_tab = CreateWindowA(WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE, cr.left, cr.top,
                          cr.right - cr.left, cr.bottom - cr.top, frame, (HMENU)(INT_PTR)ID_TAB,
                          g_hinst, NULL);
    if (g_ui_font) SendMessage(g_tab, WM_SETFONT, (WPARAM)g_ui_font, TRUE);

    TCITEMA ti;
    memset(&ti, 0, sizeof(ti));
    ti.mask = TCIF_TEXT;
    ti.pszText = (char *)"Vulkan";
    SendMessage(g_tab, TCM_INSERTITEMA, 0, (LPARAM)&ti);
    ti.pszText = (char *)"OpenGL";
    SendMessage(g_tab, TCM_INSERTITEMA, 1, (LPARAM)&ti);

    RECT dr = cr;
    SendMessage(g_tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&dr);

    char *vk = aio_gpuinfo_build_vk_text();
    char *gl = aio_gpuinfo_build_gl_text();
    g_edit_vk = make_report_edit(frame, &dr, vk ? vk : "(no Vulkan data)");
    g_edit_gl = make_report_edit(frame, &dr, gl ? gl : "(no OpenGL data)");
    if (vk) free(vk);
    if (gl) free(gl);

    // Vulkan tab selected first.
    ShowWindow(g_edit_vk, SW_SHOW);
    ShowWindow(g_edit_gl, SW_HIDE);
}

static void show_placeholder(HWND frame, const char *title, const char *msg) {
    destroy_content();
    SetWindowTextA(g_header, title);
    RECT cr;
    get_content_rect(frame, &cr);
    g_placeholder = CreateWindowA("STATIC", msg, WS_CHILD | WS_VISIBLE | SS_LEFT, cr.left, cr.top,
                                  cr.right - cr.left, cr.bottom - cr.top, frame, NULL, g_hinst, NULL);
    if (g_ui_font) SendMessage(g_placeholder, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
}

static void show_benchmark(HWND frame) {
    destroy_content();
    g_cb_bench = 1;  // these buttons run a benchmark and poll for a result file
    SetWindowTextA(g_header, "Benchmark");
    RECT cr;
    get_content_rect(frame, &cr);

    g_placeholder = CreateWindowA(
        "STATIC",
        "Pick an API to benchmark (15 seconds). Results pop up (avg / min / max / 1% low FPS)\n"
        "and per-frame data is saved to AIO-Graphics-Test_bench.csv.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, cr.left, cr.top, cr.right - cr.left, 56, frame, NULL, g_hinst,
        NULL);
    if (g_ui_font) SendMessage(g_placeholder, WM_SETFONT, (WPARAM)g_ui_font, TRUE);

    static const char *labels[] = {"Benchmark:  Vulkan  (15s)", "Benchmark:  OpenGL  (15s)",
                                   "Benchmark:  Direct3D 11  (15s)"};
    static const char *args[] = {"vk --bench 15", "gl --bench 15", "dx11 --bench 15"};
    static const char *apilabels[] = {"Vulkan", "OpenGL", "Direct3D 11"};
    g_cbtn_n = 3;
    int y = cr.top + 70;
    for (int i = 0; i < g_cbtn_n; i++) {
        g_cbtn_arg[i] = args[i];
        g_cbtn_label[i] = apilabels[i];
        g_cbtn_proc[i] = NULL;
        g_cbtn[i] = CreateWindowA("BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, cr.left,
                                  y, 240, 34, frame, (HMENU)(INT_PTR)(ID_CB_FIRST + i), g_hinst, NULL);
        if (g_ui_font) SendMessage(g_cbtn[i], WM_SETFONT, (WPARAM)g_ui_font, TRUE);
        // Result label to the right of the button (filled in when the run finishes).
        g_cbtn_result[i] =
            CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT, cr.left + 252, y + 8,
                          (cr.right - (cr.left + 252)), 26, frame, NULL, g_hinst, NULL);
        if (g_ui_font) SendMessage(g_cbtn_result[i], WM_SETFONT, (WPARAM)g_ui_font, TRUE);
        y += 44;
    }
}

// Direct3D 11 test-suite picker: each button launches one DX11 scene in a new
// window. These are launch-only (no benchmark polling).
static void show_dx11_scenes(HWND frame) {
    destroy_content();
    g_cb_bench = 0;  // launch-only buttons
    SetWindowTextA(g_header, "Cube - Direct3D 11 (DXVK)");
    RECT cr;
    get_content_rect(frame, &cr);

    g_placeholder = CreateWindowA(
        "STATIC",
        "Direct3D 11 test suite (tests the DXVK path). Each opens in a new window;\n"
        "the menu stays here. Press Esc in a test window to close it.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, cr.left, cr.top, cr.right - cr.left, 56, frame, NULL,
        g_hinst, NULL);
    if (g_ui_font) SendMessage(g_placeholder, WM_SETFONT, (WPARAM)g_ui_font, TRUE);

    static const char *labels[] = {"Spinning cube", "Textured cube", "Instanced (512 cubes)"};
    static const char *args[] = {"dx11 --scene spin", "dx11 --scene textured",
                                 "dx11 --scene instanced"};
    g_cbtn_n = (int)(sizeof(args) / sizeof(args[0]));
    int y = cr.top + 70;
    for (int i = 0; i < g_cbtn_n; i++) {
        g_cbtn_arg[i] = args[i];
        g_cbtn_label[i] = NULL;
        g_cbtn_proc[i] = NULL;
        g_cbtn_result[i] = NULL;
        g_cbtn[i] = CreateWindowA("BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  cr.left, y, 240, 34, frame, (HMENU)(INT_PTR)(ID_CB_FIRST + i),
                                  g_hinst, NULL);
        if (g_ui_font) SendMessage(g_cbtn[i], WM_SETFONT, (WPARAM)g_ui_font, TRUE);
        y += 44;
    }
}

static void layout_content(HWND frame) {
    RECT cr;
    get_content_rect(frame, &cr);
    RECT hr;
    GetClientRect(frame, &hr);
    MoveWindow(g_header, SB_W + 10, 8, hr.right - (SB_W + 10) - 10, HEADER_H - 4, TRUE);
    if (g_tab) {
        MoveWindow(g_tab, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top, TRUE);
        RECT dr = cr;
        SendMessage(g_tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&dr);
        if (g_edit_vk) MoveWindow(g_edit_vk, dr.left, dr.top, dr.right - dr.left, dr.bottom - dr.top, TRUE);
        if (g_edit_gl) MoveWindow(g_edit_gl, dr.left, dr.top, dr.right - dr.left, dr.bottom - dr.top, TRUE);
    }
    if (g_placeholder)
        MoveWindow(g_placeholder, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top, TRUE);
}

// Launches a cube/benchmark in a new window. Returns the process handle (caller
// closes it) or NULL on failure.
static HANDLE launch_cube_window(const char *api) {
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char cmd[MAX_PATH + 40];
    snprintf(cmd, sizeof(cmd), "\"%s\" --cube %s", exe, api);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        return pi.hProcess;
    }
    return NULL;
}

static void on_select(HWND frame, int action) {
    switch (action) {
        case AIO_MODE_EXIT:
            DestroyWindow(frame);
            break;
        case AIO_MODE_GPUINFO:
            show_gpuinfo(frame);
            break;
        case AIO_MODE_CUBE_VK: {
            HANDLE h = launch_cube_window("vk");
            if (h) CloseHandle(h);
            show_placeholder(frame, "Cube - Vulkan",
                             "Launched the Vulkan cube in a new window.\n\n"
                             "The menu stays here - switch back any time, or launch another test.");
            break;
        }
        case AIO_MODE_CUBE_GL: {
            HANDLE h = launch_cube_window("gl");
            if (h) CloseHandle(h);
            show_placeholder(frame, "Cube - OpenGL",
                             "Launched the OpenGL cube in a new window.\n\n"
                             "The menu stays here - switch back any time, or launch another test.");
            break;
        }
        case AIO_MODE_CUBE_DX11:
            show_dx11_scenes(frame);  // pick a scene from the DX11 test suite
            break;
        case AIO_MODE_CUBE_DX9:
        case AIO_MODE_CUBE_DX12:
            show_placeholder(frame, "Cube",
                             "This graphics API backend is coming in a future version.\n\n"
                             "Available now: Cube (Vulkan), Cube (OpenGL), Cube (Direct3D 11), and GPU Info.");
            break;
        case AIO_MODE_BENCH:
            show_benchmark(frame);
            break;
        default:
            break;
    }
}

static LRESULT CALLBACK shell_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            for (int i = 0; i < NITEMS; i++) {
                g_sidebar[i] = CreateWindowA("BUTTON", g_items[i].label,
                                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 6, 6 + i * 30,
                                             SB_W - 12, 26, hwnd, (HMENU)(INT_PTR)(ID_FIRST_BUTTON + i),
                                             g_hinst, NULL);
                if (g_ui_font) SendMessage(g_sidebar[i], WM_SETFONT, (WPARAM)g_ui_font, TRUE);
            }
            g_header = CreateWindowA("STATIC", "AIO Graphics Test", WS_CHILD | WS_VISIBLE | SS_LEFT, SB_W + 10,
                                     8, 300, HEADER_H - 4, hwnd, NULL, g_hinst, NULL);
            if (g_header_font) SendMessage(g_header, WM_SETFONT, (WPARAM)g_header_font, TRUE);
            show_placeholder(hwnd, "AIO Graphics Test",
                             "Select a test from the menu on the left.\n\n"
                             "GPU Info opens here; cube tests open in a new window.");
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int cb = id - ID_CB_FIRST;
            if (cb >= 0 && cb < g_cbtn_n) {  // content-area buttons
                if (g_cb_bench) {            // Benchmark view: poll for a result file
                    if (g_cbtn_proc[cb]) CloseHandle(g_cbtn_proc[cb]);
                    g_cbtn_proc[cb] = launch_cube_window(g_cbtn_arg[cb]);
                    if (g_cbtn_result[cb]) SetWindowTextA(g_cbtn_result[cb], "running...");
                    SetTimer(hwnd, 1, 500, NULL);
                } else {  // Scene picker: fire-and-forget launch in a new window
                    HANDLE h = launch_cube_window(g_cbtn_arg[cb]);
                    if (h) CloseHandle(h);
                }
                return 0;
            }
            int idx = id - ID_FIRST_BUTTON;
            if (idx >= 0 && idx < NITEMS) on_select(hwnd, g_items[idx].action);
            return 0;
        }
        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lParam;
            if (g_tab && nh->hwndFrom == g_tab && nh->code == TCN_SELCHANGE) {
                int sel = (int)SendMessage(g_tab, TCM_GETCURSEL, 0, 0);
                ShowWindow(g_edit_vk, sel == 0 ? SW_SHOW : SW_HIDE);
                ShowWindow(g_edit_gl, sel == 1 ? SW_SHOW : SW_HIDE);
            }
            return 0;
        }
        case WM_TIMER: {
            // Poll running benchmark processes; when one exits, show its result.
            for (int i = 0; i < g_cbtn_n; i++) {
                if (g_cbtn_proc[i] && WaitForSingleObject(g_cbtn_proc[i], 0) == WAIT_OBJECT_0) {
                    CloseHandle(g_cbtn_proc[i]);
                    g_cbtn_proc[i] = NULL;
                    char path[160], buf[256];
                    snprintf(path, sizeof(path), "AIO-Graphics-Test_bench_%s.txt", g_cbtn_label[i]);
                    FILE *f = fopen(path, "r");
                    if (f) {
                        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
                        buf[n] = '\0';
                        fclose(f);
                        if (g_cbtn_result[i]) SetWindowTextA(g_cbtn_result[i], buf);
                    } else if (g_cbtn_result[i]) {
                        SetWindowTextA(g_cbtn_result[i], "(no result file)");
                    }
                }
            }
            return 0;
        }
        case WM_SIZE:
            layout_content(hwnd);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int aio_run_shell(HINSTANCE hInstance) {
    g_hinst = hInstance;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    g_ui_font = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_header_font = CreateFontA(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_mono_font = CreateFontA(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              FIXED_PITCH | FF_MODERN, "Consolas");

    const char *cls = "AIOGraphicsTestShell";
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = shell_wndproc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);

    int w = 680, h = 480;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;

    HWND hwnd = CreateWindowA(cls, "AIO Graphics Test", WS_OVERLAPPEDWINDOW, sx, sy, w, h, NULL, NULL,
                              hInstance, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (IsDialogMessage(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_ui_font) DeleteObject(g_ui_font);
    if (g_header_font) DeleteObject(g_header_font);
    if (g_mono_font) DeleteObject(g_mono_font);
    UnregisterClassA(cls, hInstance);
    return 0;
}
