// AIO Graphics Test - OpenGL cube backend.
//
// A spinning, multi-colored cube rendered with fixed-function OpenGL (GL 1.1,
// no extension loading) so it runs through whatever GL the container provides
// (e.g. Zink/wined3d -> Vulkan -> Turnip under Winlator). Shares the in-window
// HUD overlay and the benchmark module with the Vulkan backend.
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#include <windows.h>
#include <GL/gl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cube_gl.h"
#include "hud.h"
#include "bench.h"
#include "watchdog.h"

static int g_w = 640, g_h = 480;
static int g_quit;

static LRESULT CALLBACK gl_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE:
            g_w = LOWORD(l);
            g_h = HIWORD(l);
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) {
                g_quit = 1;
                PostQuitMessage(0);
            }
            return 0;
        case WM_CLOSE:
            g_quit = 1;
            PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcA(h, m, w, l);
}

static void draw_face(float r, float g, float b, const float v[4][3]) {
    glColor3f(r, g, b);
    glVertex3fv(v[0]);
    glVertex3fv(v[1]);
    glVertex3fv(v[2]);
    glVertex3fv(v[3]);
}

static void draw_cube(float angle) {
    glViewport(0, 0, g_w, g_h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    double aspect = (g_h > 0) ? (double)g_w / (double)g_h : 1.0;
    double znear = 0.1, zfar = 100.0;
    double top = 0.0414;  // ~ znear * tan(22.5 deg), 45 deg vertical FOV
    double right = top * aspect;
    glFrustum(-right, right, -top, top, znear, zfar);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(angle, 1.0f, 1.0f, 0.0f);
    glRotatef(angle * 0.5f, 0.0f, 1.0f, 0.0f);

    static const float f[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},        // front
        {{-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {1, -1, -1}},    // back
        {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},        // top
        {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},    // bottom
        {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},        // right
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},    // left
    };

    glBegin(GL_QUADS);
    draw_face(0.90f, 0.20f, 0.20f, f[0]);
    draw_face(0.20f, 0.80f, 0.30f, f[1]);
    draw_face(0.25f, 0.45f, 0.95f, f[2]);
    draw_face(0.95f, 0.80f, 0.20f, f[3]);
    draw_face(0.85f, 0.40f, 0.90f, f[4]);
    draw_face(0.20f, 0.85f, 0.90f, f[5]);
    glEnd();

    glFlush();
}

int aio_run_gl_cube(HINSTANCE hinst) {
    const char *api = "OpenGL";
    const char *cls = "AIOGlCube";

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = gl_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hinst, MAKEINTRESOURCEA(1));
    wc.lpszClassName = cls;
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(cls, "AIO Graphics Test  -  OpenGL", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, hinst, NULL);
    if (!hwnd) return 1;

    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(dc, &pfd);
    HGLRC rc = NULL;
    if (pf == 0 || !SetPixelFormat(dc, pf, &pfd) || !(rc = wglCreateContext(dc)) ||
        !wglMakeCurrent(dc, rc)) {
        MessageBoxA(NULL,
                    "OpenGL is not available in this container.\n\n"
                    "Could not create a WGL context (no GL driver / pixel format).",
                    "AIO Graphics Test - OpenGL", MB_OK | MB_ICONERROR);
        if (rc) wglDeleteContext(rc);
        ReleaseDC(hwnd, dc);
        DestroyWindow(hwnd);
        return 1;
    }

    // Vsync via WGL_EXT_swap_control (if present): 1 = vsync, 0 = uncapped.
    typedef BOOL(WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int);
    PFNWGLSWAPINTERVALEXTPROC p_swap_interval =
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (p_swap_interval) p_swap_interval(aio_vsync ? 1 : 0);

    aio_hud_create(hinst);
    aio_hud_update(hwnd, "OpenGL  -  measuring...");

    int bench_on = aio_bench_active();
    LARGE_INTEGER qpf, start, prev;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&start);
    prev = start;

    ULONGLONG last_ms = GetTickCount64();
    ULONGLONG start_ms = last_ms;
    uint64_t frames = 0, last_frame = 0;

    MSG msg;
    aio_watchdog_start(&frames, 12);
    g_quit = 0;
    while (!g_quit) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_quit = 1;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (g_quit) break;

        float angle = (float)((GetTickCount64() - start_ms) % 36000ULL) * 0.05f;
        draw_cube(angle);
        SwapBuffers(dc);
        frames++;

        if (bench_on) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double dt_ms = (double)(now.QuadPart - prev.QuadPart) * 1000.0 / (double)qpf.QuadPart;
            aio_bench_add(dt_ms);
            prev = now;
            double elapsed = (double)(now.QuadPart - start.QuadPart) / (double)qpf.QuadPart;
            if (elapsed >= (double)aio_bench_seconds()) g_quit = 1;
        }

        ULONGLONG now_ms = GetTickCount64();
        if (now_ms - last_ms >= 500) {
            double secs = (double)(now_ms - last_ms) / 1000.0;
            double fps = (secs > 0.0) ? (double)(frames - last_frame) / secs : 0.0;
            char hud[64], title[128];
            snprintf(hud, sizeof(hud), "%s   %.0f FPS", api, fps);
            aio_hud_update(hwnd, hud);
            snprintf(title, sizeof(title), "AIO Graphics Test  -  %s  -  %.0f FPS", api, fps);
            SetWindowTextA(hwnd, title);
            last_ms = now_ms;
            last_frame = frames;
        }
    }

    aio_watchdog_stop();

    if (bench_on) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double total = (double)(now.QuadPart - start.QuadPart) / (double)qpf.QuadPart;
        char *res = aio_bench_finish(api, total);
        if (res) {
            aio_bench_show_result(res);
            free(res);
        }
    }

    aio_hud_destroy();
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return 0;
}
