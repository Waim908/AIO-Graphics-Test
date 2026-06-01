// AIO Graphics Test - Direct3D 11 cube backend.
//
// A spinning, multi-colored cube rendered with Direct3D 11. Under Winlator this
// exercises the DXVK (d3d11.dll -> Vulkan -> Turnip) translation path, so it is
// the natural counterpart to the OpenGL (wined3d/Zink) and native Vulkan cubes.
// Shares the in-window HUD overlay and the benchmark module with the other
// backends.
//
// HLSL shaders are compiled at runtime via d3dcompiler_47.dll (a Wine builtin in
// the container), loaded dynamically so we don't hard-link a specific version.
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cube_d3d11.h"
#include "hud.h"
#include "bench.h"

static int g_w = 640, g_h = 480;
static int g_quit;

static LRESULT CALLBACK d3d11_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
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

// --- Minimal row-major / row-vector matrix math (v' = v * M), matching the D3D
//     convention used by the row_major cbuffer in the shader below. ---
typedef struct {
    float m[16];
} Mat4;

static Mat4 mat_identity(void) {
    Mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static Mat4 mat_mul(Mat4 a, Mat4 b) {
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

static Mat4 mat_translate(float x, float y, float z) {
    Mat4 r = mat_identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

// Row-vector rotation about a unit axis (x,y,z).
static Mat4 mat_rotate(float ax, float ay, float az, float angle_rad) {
    float len = sqrtf(ax * ax + ay * ay + az * az);
    if (len > 0.0f) {
        ax /= len;
        ay /= len;
        az /= len;
    }
    float c = cosf(angle_rad), s = sinf(angle_rad), t = 1.0f - c;
    Mat4 r = mat_identity();
    r.m[0] = c + ax * ax * t;
    r.m[1] = ax * ay * t + az * s;
    r.m[2] = ax * az * t - ay * s;
    r.m[4] = ay * ax * t - az * s;
    r.m[5] = c + ay * ay * t;
    r.m[6] = ay * az * t + ax * s;
    r.m[8] = az * ax * t + ay * s;
    r.m[9] = az * ay * t - ax * s;
    r.m[10] = c + az * az * t;
    return r;
}

// Right-handed perspective with clip-space z in [0,1] (Direct3D convention).
static Mat4 mat_perspective(float fovy_rad, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    Mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = zn * zf / (zn - zf);
    return r;
}

typedef struct {
    float pos[3];
    float col[3];
} Vertex;

// Builds the 36-vertex (6 faces x 2 triangles) colored cube.
static void build_cube(Vertex *out) {
    static const float f[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},      // front
        {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},  // back
        {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},      // top
        {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},  // bottom
        {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},      // right
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},  // left
    };
    static const float col[6][3] = {
        {0.90f, 0.20f, 0.20f}, {0.20f, 0.80f, 0.30f}, {0.25f, 0.45f, 0.95f},
        {0.95f, 0.80f, 0.20f}, {0.85f, 0.40f, 0.90f}, {0.20f, 0.85f, 0.90f},
    };
    int v = 0;
    for (int face = 0; face < 6; face++) {
        const int idx[6] = {0, 1, 2, 0, 2, 3};  // two triangles per quad
        for (int k = 0; k < 6; k++) {
            int ci = idx[k];
            out[v].pos[0] = f[face][ci][0];
            out[v].pos[1] = f[face][ci][1];
            out[v].pos[2] = f[face][ci][2];
            out[v].col[0] = col[face][0];
            out[v].col[1] = col[face][1];
            out[v].col[2] = col[face][2];
            v++;
        }
    }
}

static const char *kHLSL =
    "cbuffer CB : register(b0) { row_major float4x4 mvp; };\n"
    "struct VSIn { float3 pos : POSITION; float3 col : COLOR; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float3 col : COLOR; };\n"
    "VSOut VSMain(VSIn i) { VSOut o; o.pos = mul(float4(i.pos, 1.0), mvp); o.col = i.col; return o; }\n"
    "float4 PSMain(VSOut i) : SV_TARGET { return float4(i.col, 1.0); }\n";

typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *,
                                        ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **,
                                        ID3DBlob **);

// D3D11CreateDeviceAndSwapChain is loaded dynamically (NOT statically linked) so
// the whole .exe has no hard import dependency on d3d11.dll / dxgi.dll - those
// only exist in the container when DXVK is installed, and a static import would
// stop the exe from launching at all (even the shell) when they're absent.
typedef HRESULT(WINAPI *PFN_D3D11CreateDeviceAndSwapChain)(
    IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *,
    ID3D11DeviceContext **);

static void fail_box(const char *msg) {
    MessageBoxA(NULL, msg, "AIO Graphics Test - Direct3D 11", MB_OK | MB_ICONERROR);
}

int aio_run_d3d11_cube(HINSTANCE hinst) {
    const char *api = "Direct3D 11";
    const char *cls = "AIOD3D11Cube";

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = d3d11_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(cls, "AIO Graphics Test  -  Direct3D 11",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 640,
                              480, NULL, NULL, hinst, NULL);
    if (!hwnd) return 1;

    RECT rc;
    GetClientRect(hwnd, &rc);
    g_w = rc.right - rc.left;
    g_h = rc.bottom - rc.top;
    if (g_w <= 0) g_w = 640;
    if (g_h <= 0) g_h = 480;

    // --- Device + swapchain ---
    DXGI_SWAP_CHAIN_DESC scd;
    memset(&scd, 0, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Width = g_w;
    scd.BufferDesc.Height = g_h;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    HMODULE d3d11lib = LoadLibraryA("d3d11.dll");
    PFN_D3D11CreateDeviceAndSwapChain p_create =
        d3d11lib ? (PFN_D3D11CreateDeviceAndSwapChain)GetProcAddress(d3d11lib,
                                                                     "D3D11CreateDeviceAndSwapChain")
                 : NULL;
    if (!p_create) {
        fail_box(
            "Direct3D 11 is not available in this container.\n\n"
            "Could not load d3d11.dll (is DXVK installed?).");
        DestroyWindow(hwnd);
        return 1;
    }

    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                      D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDXGISwapChain *swap = NULL;

    HRESULT hr = p_create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, want,
                          (UINT)(sizeof(want) / sizeof(want[0])), D3D11_SDK_VERSION, &scd, &swap,
                          &dev, &got, &ctx);
    if (FAILED(hr)) {
        fail_box(
            "Direct3D 11 is not available in this container.\n\n"
            "Could not create a D3D11 device + swapchain (no d3d11.dll / DXVK?).");
        DestroyWindow(hwnd);
        return 1;
    }

    // --- Render target view from the backbuffer ---
    ID3D11Texture2D *backbuf = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&backbuf);
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)backbuf, NULL, &rtv);

    // --- Depth/stencil buffer + view (correct occlusion regardless of winding) ---
    D3D11_TEXTURE2D_DESC dd;
    memset(&dd, 0, sizeof(dd));
    dd.Width = g_w;
    dd.Height = g_h;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D *depth_tex = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11Device_CreateTexture2D(dev, &dd, NULL, &depth_tex);
    ID3D11Device_CreateDepthStencilView(dev, (ID3D11Resource *)depth_tex, NULL, &dsv);

    D3D11_DEPTH_STENCIL_DESC dsd;
    memset(&dsd, 0, sizeof(dsd));
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS;
    ID3D11DepthStencilState *dss = NULL;
    ID3D11Device_CreateDepthStencilState(dev, &dsd, &dss);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, dss, 1);

    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, dsv);

    D3D11_VIEWPORT vp;
    memset(&vp, 0, sizeof(vp));
    vp.Width = (float)g_w;
    vp.Height = (float)g_h;
    vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);

    // --- Compile shaders (runtime, via d3dcompiler_47.dll) ---
    HMODULE d3dc = LoadLibraryA("d3dcompiler_47.dll");
    if (!d3dc) d3dc = LoadLibraryA("d3dcompiler_43.dll");
    PFN_D3DCompile p_compile = d3dc ? (PFN_D3DCompile)GetProcAddress(d3dc, "D3DCompile") : NULL;
    if (!p_compile) {
        fail_box(
            "Could not load d3dcompiler (D3DCompile) in this container.\n\n"
            "The HLSL shaders for the Direct3D 11 cube can't be compiled.");
        DestroyWindow(hwnd);
        return 1;
    }

    ID3DBlob *vsb = NULL, *psb = NULL, *err = NULL;
    hr = p_compile(kHLSL, strlen(kHLSL), "cube.hlsl", NULL, NULL, "VSMain", "vs_4_0", 0, 0, &vsb,
                   &err);
    if (FAILED(hr)) {
        fail_box("Vertex shader failed to compile.");
        DestroyWindow(hwnd);
        return 1;
    }
    if (err) {
        ID3D10Blob_Release(err);
        err = NULL;
    }
    hr = p_compile(kHLSL, strlen(kHLSL), "cube.hlsl", NULL, NULL, "PSMain", "ps_4_0", 0, 0, &psb,
                   &err);
    if (FAILED(hr)) {
        fail_box("Pixel shader failed to compile.");
        DestroyWindow(hwnd);
        return 1;
    }
    if (err) ID3D10Blob_Release(err);

    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &ps);

    // --- Input layout ---
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11InputLayout *layout = NULL;
    ID3D11Device_CreateInputLayout(dev, il, 2, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &layout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);

    // --- Vertex buffer ---
    Vertex verts[36];
    build_cube(verts);
    D3D11_BUFFER_DESC vbd;
    memset(&vbd, 0, sizeof(vbd));
    vbd.ByteWidth = sizeof(verts);
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsr;
    memset(&vsr, 0, sizeof(vsr));
    vsr.pSysMem = verts;
    ID3D11Buffer *vbo = NULL;
    ID3D11Device_CreateBuffer(dev, &vbd, &vsr, &vbo);

    // --- Constant buffer (the MVP matrix, updated each frame) ---
    D3D11_BUFFER_DESC cbd;
    memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = sizeof(Mat4);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer *cbo = NULL;
    ID3D11Device_CreateBuffer(dev, &cbd, NULL, &cbo);

    // --- Bind the pipeline (state is constant for the whole run) ---
    UINT stride = sizeof(Vertex), offset = 0;
    ID3D11DeviceContext_IASetInputLayout(ctx, layout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbo, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cbo);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);

    aio_hud_create(hinst);
    aio_hud_update(hwnd, "Direct3D 11  -  measuring...");

    int bench_on = aio_bench_active();
    LARGE_INTEGER qpf, start, prev;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&start);
    prev = start;

    ULONGLONG last_ms = GetTickCount64();
    ULONGLONG start_ms = last_ms;
    uint64_t frames = 0, last_frame = 0;

    const float clear[4] = {0.10f, 0.10f, 0.12f, 1.0f};
    float aspect = (g_h > 0) ? (float)g_w / (float)g_h : 1.0f;
    Mat4 proj = mat_perspective(0.7854f /* 45 deg */, aspect, 0.1f, 100.0f);
    Mat4 view = mat_translate(0.0f, 0.0f, -5.0f);

    MSG msg;
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

        float angle = (float)((GetTickCount64() - start_ms) % 36000ULL) * 0.05f * 0.0174533f;
        Mat4 model = mat_mul(mat_rotate(1, 1, 0, angle), mat_rotate(0, 1, 0, angle * 0.5f));
        Mat4 mvp = mat_mul(mat_mul(model, view), proj);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cbo, 0,
                                              D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, mvp.m, sizeof(mvp.m));
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cbo, 0);
        }

        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear);
        ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        ID3D11DeviceContext_Draw(ctx, 36, 0);
        IDXGISwapChain_Present(swap, 0, 0);
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

    if (bench_on) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double total = (double)(now.QuadPart - start.QuadPart) / (double)qpf.QuadPart;
        char *res = aio_bench_finish(api, total);
        if (res) {
            MessageBoxA(NULL, res, "AIO Graphics Test - Benchmark", MB_OK | MB_ICONINFORMATION);
            free(res);
        }
    }

    aio_hud_destroy();

    if (cbo) ID3D11Buffer_Release(cbo);
    if (vbo) ID3D11Buffer_Release(vbo);
    if (layout) ID3D11InputLayout_Release(layout);
    if (ps) ID3D11PixelShader_Release(ps);
    if (vs) ID3D11VertexShader_Release(vs);
    if (dss) ID3D11DepthStencilState_Release(dss);
    if (dsv) ID3D11DepthStencilView_Release(dsv);
    if (depth_tex) ID3D11Texture2D_Release(depth_tex);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (backbuf) ID3D11Texture2D_Release(backbuf);
    if (swap) IDXGISwapChain_Release(swap);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    if (dev) ID3D11Device_Release(dev);

    DestroyWindow(hwnd);
    return 0;
}
