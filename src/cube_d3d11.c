// AIO Graphics Test - Direct3D 11 test suite.
//
// A set of Direct3D 11 scenes that each exercise a different part of the DXVK
// (d3d11.dll -> Vulkan -> Turnip) path under Winlator:
//   spin       - baseline spinning colored cube (pipeline smoke test)
//   textured   - cube with a generated texture (texture upload + SRV + sampler)
//   instanced  - hundreds of cubes via instanced draw (throughput / benchmark)
//   tess       - tessellated shape (hull/domain shaders)         [stage 2]
//   compute    - compute-shader particle sim                     [stage 3]
//
// Shared scaffolding (window, device, swapchain, depth buffer, HUD overlay,
// benchmark loop) lives in the runner; each scene only provides its shaders,
// resources, and per-frame draw via a small D3D11Scene interface.
//
// HLSL is compiled at runtime via d3dcompiler_47.dll, and d3d11.dll is loaded
// dynamically, so the .exe has NO static dependency on either - it launches
// fine without DXVK and shows a graceful notice instead.
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
#include "dolphin_assets.h"  // embedded DolphinVS mesh/texture/caustic data
#include "hud.h"
#include "bench.h"
#include "watchdog.h"

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
//     convention used by the row_major cbuffers in the shaders below. ---
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

static Mat4 mat_scale(float s) {
    Mat4 r = mat_identity();
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}

// Right-handed look-at (row-vector world->view), matching the perspective below.
static Mat4 mat_lookat(float ex, float ey, float ez, float ax, float ay, float az, float ux,
                       float uy, float uz) {
    float zx = ex - ax, zy = ey - ay, zz = ez - az;
    float zl = sqrtf(zx * zx + zy * zy + zz * zz);
    zx /= zl; zy /= zl; zz /= zl;
    float xx = uy * zz - uz * zy, xy = uz * zx - ux * zz, xz = ux * zy - uy * zx;
    float xl = sqrtf(xx * xx + xy * xy + xz * xz);
    xx /= xl; xy /= xl; xz /= xl;
    float yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    Mat4 r = mat_identity();
    r.m[0] = xx; r.m[1] = yx; r.m[2] = zx;
    r.m[4] = xy; r.m[5] = yy; r.m[6] = zy;
    r.m[8] = xz; r.m[9] = yz; r.m[10] = zz;
    r.m[12] = -(xx * ex + xy * ey + xz * ez);
    r.m[13] = -(yx * ex + yy * ey + yz * ez);
    r.m[14] = -(zx * ex + zy * ey + zz * ez);
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

// --- Shared shader-compile helper (dynamic d3dcompiler) ---
typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *,
                                        ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **,
                                        ID3DBlob **);

// D3D11CreateDeviceAndSwapChain is loaded dynamically (NOT statically linked) so
// the whole .exe has no hard import dependency on d3d11.dll / dxgi.dll.
typedef HRESULT(WINAPI *PFN_D3D11CreateDeviceAndSwapChain)(
    IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *,
    ID3D11DeviceContext **);

static PFN_D3DCompile g_compile;

static void fail_box(const char *msg) {
    MessageBoxA(NULL, msg, "AIO Graphics Test - Direct3D 11", MB_OK | MB_ICONERROR);
}

// Compiles one HLSL entry point to a blob. Returns NULL (and shows a box) on error.
static ID3DBlob *compile_hlsl(const char *src, const char *entry, const char *target) {
    ID3DBlob *blob = NULL, *err = NULL;
    HRESULT hr = g_compile(src, strlen(src), "scene.hlsl", NULL, NULL, entry, target, 0, 0, &blob,
                           &err);
    if (FAILED(hr)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Shader '%s' (%s) failed to compile.\n\n%s", entry, target,
                 err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "");
        fail_box(msg);
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    return blob;
}

// --- Scene interface ---
// init: build resources (return 0 on success). frame: update + issue draws for
// one frame (RTV+DSV already bound and cleared). cleanup: release resources.
typedef struct {
    const char *name;   // selector ("spin", ...)
    const char *label;  // HUD/title label ("D3D11 Cube", ...)
    int (*init)(ID3D11Device *dev, ID3D11DeviceContext *ctx, int w, int h);
    void (*frame)(ID3D11DeviceContext *ctx, double t, float aspect);
    void (*cleanup)(void);
} D3D11Scene;

// ============================ shared cube geometry ============================
typedef struct {
    float pos[3];
    float col[3];
} ColVertex;

static const float kFace[6][4][3] = {
    {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},      // front
    {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},  // back
    {{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},      // top
    {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},  // bottom
    {{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},      // right
    {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},  // left
};
static const float kFaceCol[6][3] = {
    {0.90f, 0.20f, 0.20f}, {0.20f, 0.80f, 0.30f}, {0.25f, 0.45f, 0.95f},
    {0.95f, 0.80f, 0.20f}, {0.85f, 0.40f, 0.90f}, {0.20f, 0.85f, 0.90f},
};
static const int kQuadIdx[6] = {0, 1, 2, 0, 2, 3};

static void build_color_cube(ColVertex *out) {
    int v = 0;
    for (int face = 0; face < 6; face++)
        for (int k = 0; k < 6; k++) {
            int ci = kQuadIdx[k];
            out[v].pos[0] = kFace[face][ci][0];
            out[v].pos[1] = kFace[face][ci][1];
            out[v].pos[2] = kFace[face][ci][2];
            out[v].col[0] = kFaceCol[face][0];
            out[v].col[1] = kFaceCol[face][1];
            out[v].col[2] = kFaceCol[face][2];
            v++;
        }
}

// ================================ SPIN scene =================================
static const char *kSpinHLSL =
    "cbuffer CB : register(b0) { row_major float4x4 mvp; };\n"
    "struct VSIn { float3 pos : POSITION; float3 col : COLOR; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float3 col : COLOR; };\n"
    "VSOut VSMain(VSIn i) { VSOut o; o.pos = mul(float4(i.pos,1.0), mvp); o.col = i.col; return o; }\n"
    "float4 PSMain(VSOut i) : SV_TARGET { return float4(i.col, 1.0); }\n";

static struct {
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *vbo, *cbo;
} g_spin;

static int spin_init(ID3D11Device *dev, ID3D11DeviceContext *ctx, int w, int h) {
    (void)ctx;
    (void)w;
    (void)h;
    ID3DBlob *vsb = compile_hlsl(kSpinHLSL, "VSMain", "vs_4_0");
    ID3DBlob *psb = compile_hlsl(kSpinHLSL, "PSMain", "ps_4_0");
    if (!vsb || !psb) return 1;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_spin.vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_spin.ps);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11Device_CreateInputLayout(dev, il, 2, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &g_spin.layout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);

    ColVertex verts[36];
    build_color_cube(verts);
    D3D11_BUFFER_DESC vbd;
    memset(&vbd, 0, sizeof(vbd));
    vbd.ByteWidth = sizeof(verts);
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr;
    memset(&sr, 0, sizeof(sr));
    sr.pSysMem = verts;
    ID3D11Device_CreateBuffer(dev, &vbd, &sr, &g_spin.vbo);

    D3D11_BUFFER_DESC cbd;
    memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = sizeof(Mat4);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(dev, &cbd, NULL, &g_spin.cbo);
    return 0;
}

static void upload_mat(ID3D11DeviceContext *ctx, ID3D11Buffer *cbo, Mat4 m) {
    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cbo, 0, D3D11_MAP_WRITE_DISCARD, 0,
                                          &map))) {
        memcpy(map.pData, m.m, sizeof(m.m));
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cbo, 0);
    }
}

static void spin_frame(ID3D11DeviceContext *ctx, double t, float aspect) {
    // Turntable: fixed forward tilt + slow spin about the vertical axis, narrow
    // FOV + camera pulled back -> reads as the classic 3-faces-visible cube.
    float a = (float)t * 0.6f;
    Mat4 model = mat_mul(mat_rotate(0, 1, 0, a), mat_rotate(1, 0, 0, 0.5f));
    Mat4 mvp = mat_mul(mat_mul(model, mat_translate(0, 0, -6.5f)),
                       mat_perspective(0.6f, aspect, 0.1f, 100.0f));
    upload_mat(ctx, g_spin.cbo, mvp);

    UINT stride = sizeof(ColVertex), offset = 0;
    ID3D11DeviceContext_IASetInputLayout(ctx, g_spin.layout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &g_spin.vbo, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, g_spin.vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &g_spin.cbo);
    ID3D11DeviceContext_PSSetShader(ctx, g_spin.ps, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 36, 0);
}

static void spin_cleanup(void) {
    if (g_spin.cbo) ID3D11Buffer_Release(g_spin.cbo);
    if (g_spin.vbo) ID3D11Buffer_Release(g_spin.vbo);
    if (g_spin.layout) ID3D11InputLayout_Release(g_spin.layout);
    if (g_spin.ps) ID3D11PixelShader_Release(g_spin.ps);
    if (g_spin.vs) ID3D11VertexShader_Release(g_spin.vs);
    memset(&g_spin, 0, sizeof(g_spin));
}

// ============================== TEXTURED scene ==============================
static const char *kTexHLSL =
    "cbuffer CB : register(b0) { row_major float4x4 mvp; };\n"
    "Texture2D tex : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };\n"
    "VSOut VSMain(VSIn i) { VSOut o; o.pos = mul(float4(i.pos,1.0), mvp); o.uv = i.uv; return o; }\n"
    "float4 PSMain(VSOut i) : SV_TARGET { return tex.Sample(smp, i.uv); }\n";

typedef struct {
    float pos[3];
    float uv[2];
} TexVertex;

static struct {
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *vbo, *cbo;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    ID3D11SamplerState *smp;
} g_tex;

static void build_tex_cube(TexVertex *out) {
    static const float uvq[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    int v = 0;
    for (int face = 0; face < 6; face++)
        for (int k = 0; k < 6; k++) {
            int ci = kQuadIdx[k];
            out[v].pos[0] = kFace[face][ci][0];
            out[v].pos[1] = kFace[face][ci][1];
            out[v].pos[2] = kFace[face][ci][2];
            out[v].uv[0] = uvq[ci][0];
            out[v].uv[1] = uvq[ci][1];
            v++;
        }
}

static int tex_init(ID3D11Device *dev, ID3D11DeviceContext *ctx, int w, int h) {
    (void)ctx;
    (void)w;
    (void)h;
    ID3DBlob *vsb = compile_hlsl(kTexHLSL, "VSMain", "vs_4_0");
    ID3DBlob *psb = compile_hlsl(kTexHLSL, "PSMain", "ps_4_0");
    if (!vsb || !psb) return 1;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_tex.vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_tex.ps);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11Device_CreateInputLayout(dev, il, 2, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &g_tex.layout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);

    TexVertex verts[36];
    build_tex_cube(verts);
    D3D11_BUFFER_DESC vbd;
    memset(&vbd, 0, sizeof(vbd));
    vbd.ByteWidth = sizeof(verts);
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr;
    memset(&sr, 0, sizeof(sr));
    sr.pSysMem = verts;
    ID3D11Device_CreateBuffer(dev, &vbd, &sr, &g_tex.vbo);

    D3D11_BUFFER_DESC cbd;
    memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = sizeof(Mat4);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(dev, &cbd, NULL, &g_tex.cbo);

    // Generate a 256x256 checker + gradient RGBA texture.
    const int TS = 256;
    uint32_t *pix = (uint32_t *)malloc((size_t)TS * TS * 4);
    if (!pix) return 1;
    for (int y = 0; y < TS; y++)
        for (int x = 0; x < TS; x++) {
            int chk = ((x >> 5) ^ (y >> 5)) & 1;
            uint8_t r = (uint8_t)(chk ? 230 : 40 + x / 2);
            uint8_t g = (uint8_t)(chk ? 80 + y / 2 : 200);
            uint8_t b = (uint8_t)(chk ? 60 : 120 + (x ^ y) / 4);
            pix[y * TS + x] = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));
    td.Width = TS;
    td.Height = TS;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA tsr;
    memset(&tsr, 0, sizeof(tsr));
    tsr.pSysMem = pix;
    tsr.SysMemPitch = TS * 4;
    ID3D11Device_CreateTexture2D(dev, &td, &tsr, &g_tex.tex);
    free(pix);
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)g_tex.tex, NULL, &g_tex.srv);

    D3D11_SAMPLER_DESC sd;
    memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11Device_CreateSamplerState(dev, &sd, &g_tex.smp);
    return (g_tex.tex && g_tex.srv && g_tex.smp) ? 0 : 1;
}

static void tex_frame(ID3D11DeviceContext *ctx, double t, float aspect) {
    // Turntable view (see spin_frame) so the textured cube reads clearly as a cube.
    float a = (float)t * 0.6f;
    Mat4 model = mat_mul(mat_rotate(0, 1, 0, a), mat_rotate(1, 0, 0, 0.5f));
    Mat4 mvp = mat_mul(mat_mul(model, mat_translate(0, 0, -6.5f)),
                       mat_perspective(0.6f, aspect, 0.1f, 100.0f));
    upload_mat(ctx, g_tex.cbo, mvp);

    UINT stride = sizeof(TexVertex), offset = 0;
    ID3D11DeviceContext_IASetInputLayout(ctx, g_tex.layout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &g_tex.vbo, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, g_tex.vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &g_tex.cbo);
    ID3D11DeviceContext_PSSetShader(ctx, g_tex.ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &g_tex.srv);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &g_tex.smp);
    ID3D11DeviceContext_Draw(ctx, 36, 0);
}

static void tex_cleanup(void) {
    if (g_tex.smp) ID3D11SamplerState_Release(g_tex.smp);
    if (g_tex.srv) ID3D11ShaderResourceView_Release(g_tex.srv);
    if (g_tex.tex) ID3D11Texture2D_Release(g_tex.tex);
    if (g_tex.cbo) ID3D11Buffer_Release(g_tex.cbo);
    if (g_tex.vbo) ID3D11Buffer_Release(g_tex.vbo);
    if (g_tex.layout) ID3D11InputLayout_Release(g_tex.layout);
    if (g_tex.ps) ID3D11PixelShader_Release(g_tex.ps);
    if (g_tex.vs) ID3D11VertexShader_Release(g_tex.vs);
    memset(&g_tex, 0, sizeof(g_tex));
}

// ============================== INSTANCED scene =============================
// A grid of cubes drawn with one instanced draw call (per-instance offset in a
// second vertex buffer). Good throughput / benchmark load.
#define INST_AXIS 8
#define INST_COUNT (INST_AXIS * INST_AXIS * INST_AXIS)

static const char *kInstHLSL =
    "cbuffer CB : register(b0) { row_major float4x4 viewproj; };\n"
    "struct VSIn { float3 pos : POSITION; float3 col : COLOR; float3 inst : INSTOFF; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float3 col : COLOR; };\n"
    "VSOut VSMain(VSIn i) {\n"
    "  VSOut o; float3 world = i.pos * 0.45 + i.inst;\n"
    "  o.pos = mul(float4(world,1.0), viewproj); o.col = i.col; return o; }\n"
    "float4 PSMain(VSOut i) : SV_TARGET { return float4(i.col, 1.0); }\n";

static struct {
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *vbo, *inst, *cbo;
} g_inst;

static int inst_init(ID3D11Device *dev, ID3D11DeviceContext *ctx, int w, int h) {
    (void)ctx;
    (void)w;
    (void)h;
    ID3DBlob *vsb = compile_hlsl(kInstHLSL, "VSMain", "vs_4_0");
    ID3DBlob *psb = compile_hlsl(kInstHLSL, "PSMain", "ps_4_0");
    if (!vsb || !psb) return 1;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_inst.vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_inst.ps);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"INSTOFF", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    ID3D11Device_CreateInputLayout(dev, il, 3, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &g_inst.layout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);

    ColVertex verts[36];
    build_color_cube(verts);
    D3D11_BUFFER_DESC vbd;
    memset(&vbd, 0, sizeof(vbd));
    vbd.ByteWidth = sizeof(verts);
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr;
    memset(&sr, 0, sizeof(sr));
    sr.pSysMem = verts;
    ID3D11Device_CreateBuffer(dev, &vbd, &sr, &g_inst.vbo);

    // Per-instance offsets: centered grid.
    float(*off)[3] = (float(*)[3])malloc(sizeof(float) * 3 * INST_COUNT);
    if (!off) return 1;
    int n = 0;
    float span = (INST_AXIS - 1) * 1.5f;
    for (int z = 0; z < INST_AXIS; z++)
        for (int y = 0; y < INST_AXIS; y++)
            for (int x = 0; x < INST_AXIS; x++) {
                off[n][0] = x * 1.5f - span * 0.5f;
                off[n][1] = y * 1.5f - span * 0.5f;
                off[n][2] = z * 1.5f - span * 0.5f;
                n++;
            }
    D3D11_BUFFER_DESC ibd;
    memset(&ibd, 0, sizeof(ibd));
    ibd.ByteWidth = (UINT)(sizeof(float) * 3 * INST_COUNT);
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isr;
    memset(&isr, 0, sizeof(isr));
    isr.pSysMem = off;
    ID3D11Device_CreateBuffer(dev, &ibd, &isr, &g_inst.inst);
    free(off);

    D3D11_BUFFER_DESC cbd;
    memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = sizeof(Mat4);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(dev, &cbd, NULL, &g_inst.cbo);
    return 0;
}

static void inst_frame(ID3D11DeviceContext *ctx, double t, float aspect) {
    float a = (float)t * 0.3f;
    // Pull the camera back far enough to see the whole grid, and spin it.
    Mat4 world = mat_mul(mat_rotate(0, 1, 0, a), mat_rotate(1, 0, 0, a * 0.4f));
    Mat4 vp = mat_mul(mat_mul(world, mat_translate(0, 0, -28)),
                      mat_perspective(0.7854f, aspect, 0.1f, 200.0f));
    upload_mat(ctx, g_inst.cbo, vp);

    UINT strides[2] = {sizeof(ColVertex), sizeof(float) * 3};
    UINT offsets[2] = {0, 0};
    ID3D11Buffer *bufs[2] = {g_inst.vbo, g_inst.inst};
    ID3D11DeviceContext_IASetInputLayout(ctx, g_inst.layout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, bufs, strides, offsets);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, g_inst.vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &g_inst.cbo);
    ID3D11DeviceContext_PSSetShader(ctx, g_inst.ps, NULL, 0);
    ID3D11DeviceContext_DrawInstanced(ctx, 36, INST_COUNT, 0, 0);
}

static void inst_cleanup(void) {
    if (g_inst.cbo) ID3D11Buffer_Release(g_inst.cbo);
    if (g_inst.inst) ID3D11Buffer_Release(g_inst.inst);
    if (g_inst.vbo) ID3D11Buffer_Release(g_inst.vbo);
    if (g_inst.layout) ID3D11InputLayout_Release(g_inst.layout);
    if (g_inst.ps) ID3D11PixelShader_Release(g_inst.ps);
    if (g_inst.vs) ID3D11VertexShader_Release(g_inst.vs);
    memset(&g_inst, 0, sizeof(g_inst));
}

// ============================= TESSELLATION scene ===========================
// An icosahedron whose 20 triangle patches are tessellated (hull/domain shaders,
// SM5 / feature level 11) and displaced onto a sphere; the tess factor animates
// 1..16 so you watch it refine from faceted to smooth.
#define PIF 3.14159265f

static const char *kTessHLSL =
    "cbuffer CB : register(b0) { row_major float4x4 mvp; float tessf; float3 pad; };\n"
    "struct VSOut { float3 pos : POSITION; };\n"
    "VSOut VSMain(float3 pos : POSITION) { VSOut o; o.pos = normalize(pos); return o; }\n"
    "struct PatchConst { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };\n"
    "PatchConst HSConst(InputPatch<VSOut,3> ip) {\n"
    "  PatchConst p; p.edges[0]=p.edges[1]=p.edges[2]=tessf; p.inside=tessf; return p; }\n"
    "[domain(\"tri\")][partitioning(\"fractional_odd\")][outputtopology(\"triangle_cw\")]\n"
    "[outputcontrolpoints(3)][patchconstantfunc(\"HSConst\")]\n"
    "VSOut HSMain(InputPatch<VSOut,3> ip, uint id : SV_OutputControlPointID) { return ip[id]; }\n"
    "struct DSOut { float4 pos : SV_POSITION; float3 col : COLOR; };\n"
    "[domain(\"tri\")]\n"
    "DSOut DSMain(PatchConst pc, float3 bary : SV_DomainLocation, const OutputPatch<VSOut,3> patch) {\n"
    "  float3 p = patch[0].pos*bary.x + patch[1].pos*bary.y + patch[2].pos*bary.z;\n"
    "  p = normalize(p); DSOut o; o.pos = mul(float4(p*1.6,1.0), mvp); o.col = p*0.5+0.5; return o; }\n"
    "float4 PSMain(DSOut i) : SV_TARGET { return float4(i.col, 1.0); }\n";

typedef struct {
    Mat4 mvp;
    float tessf;
    float pad[3];
} TessCB;

static struct {
    ID3D11VertexShader *vs;
    ID3D11HullShader *hs;
    ID3D11DomainShader *ds;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *vbo, *cbo;
} g_tess;

static int tess_init(ID3D11Device *dev, ID3D11DeviceContext *ctx, int w, int h) {
    (void)ctx;
    (void)w;
    (void)h;
    ID3DBlob *vsb = compile_hlsl(kTessHLSL, "VSMain", "vs_5_0");
    ID3DBlob *hsb = compile_hlsl(kTessHLSL, "HSMain", "hs_5_0");
    ID3DBlob *dsb = compile_hlsl(kTessHLSL, "DSMain", "ds_5_0");
    ID3DBlob *psb = compile_hlsl(kTessHLSL, "PSMain", "ps_5_0");
    if (!vsb || !hsb || !dsb || !psb) return 1;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_tess.vs);
    ID3D11Device_CreateHullShader(dev, ID3D10Blob_GetBufferPointer(hsb),
                                  ID3D10Blob_GetBufferSize(hsb), NULL, &g_tess.hs);
    ID3D11Device_CreateDomainShader(dev, ID3D10Blob_GetBufferPointer(dsb),
                                    ID3D10Blob_GetBufferSize(dsb), NULL, &g_tess.ds);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_tess.ps);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11Device_CreateInputLayout(dev, il, 1, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &g_tess.layout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(hsb);
    ID3D10Blob_Release(dsb);
    ID3D10Blob_Release(psb);
    if (!g_tess.hs || !g_tess.ds) {
        fail_box(
            "Tessellation needs Direct3D 11 feature level 11.\n\n"
            "This container's device doesn't support hull/domain shaders.");
        return 1;
    }

    const float t = 1.618034f;
    const float ico[12][3] = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
        {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
    };
    const int faces[20][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
        {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
        {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };
    float cp[60][3];
    int n = 0;
    for (int f = 0; f < 20; f++)
        for (int k = 0; k < 3; k++) {
            cp[n][0] = ico[faces[f][k]][0];
            cp[n][1] = ico[faces[f][k]][1];
            cp[n][2] = ico[faces[f][k]][2];
            n++;
        }
    D3D11_BUFFER_DESC vbd;
    memset(&vbd, 0, sizeof(vbd));
    vbd.ByteWidth = sizeof(cp);
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr;
    memset(&sr, 0, sizeof(sr));
    sr.pSysMem = cp;
    ID3D11Device_CreateBuffer(dev, &vbd, &sr, &g_tess.vbo);

    D3D11_BUFFER_DESC cbd;
    memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = sizeof(TessCB);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(dev, &cbd, NULL, &g_tess.cbo);
    return 0;
}

static void tess_frame(ID3D11DeviceContext *ctx, double t, float aspect) {
    float a = (float)t * 0.6f;
    Mat4 model = mat_mul(mat_rotate(0, 1, 0, a), mat_rotate(1, 0, 0, a * 0.4f));
    TessCB cb;
    cb.mvp = mat_mul(mat_mul(model, mat_translate(0, 0, -5)),
                     mat_perspective(0.7854f, aspect, 0.1f, 100.0f));
    cb.tessf = 1.0f + (sinf((float)t * 0.8f) * 0.5f + 0.5f) * 15.0f;  // 1..16

    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_tess.cbo, 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, &cb, sizeof(cb));
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_tess.cbo, 0);
    }

    UINT stride = sizeof(float) * 3, offset = 0;
    ID3D11DeviceContext_IASetInputLayout(ctx, g_tess.layout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &g_tess.vbo, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    ID3D11DeviceContext_VSSetShader(ctx, g_tess.vs, NULL, 0);
    ID3D11DeviceContext_HSSetShader(ctx, g_tess.hs, NULL, 0);
    ID3D11DeviceContext_DSSetShader(ctx, g_tess.ds, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, g_tess.ps, NULL, 0);
    ID3D11DeviceContext_HSSetConstantBuffers(ctx, 0, 1, &g_tess.cbo);
    ID3D11DeviceContext_DSSetConstantBuffers(ctx, 0, 1, &g_tess.cbo);
    ID3D11DeviceContext_Draw(ctx, 60, 0);
}

static void tess_cleanup(void) {
    if (g_tess.cbo) ID3D11Buffer_Release(g_tess.cbo);
    if (g_tess.vbo) ID3D11Buffer_Release(g_tess.vbo);
    if (g_tess.layout) ID3D11InputLayout_Release(g_tess.layout);
    if (g_tess.ps) ID3D11PixelShader_Release(g_tess.ps);
    if (g_tess.ds) ID3D11DomainShader_Release(g_tess.ds);
    if (g_tess.hs) ID3D11HullShader_Release(g_tess.hs);
    if (g_tess.vs) ID3D11VertexShader_Release(g_tess.vs);
    memset(&g_tess, 0, sizeof(g_tess));
}

// ============================== COMPUTE scene ===============================
// A compute shader (cs_5_0) advances a swirling particle cloud in a structured
// buffer each frame; the vertex shader reads it back by SV_VertexID and draws
// the particles as points. Exercises the entire D3D11 compute path + UAV/SRV.
#define PART_COUNT 131072  // 512 * 256
#define PART_GROUPS (PART_COUNT / 256)

static const char *kCompCS =
    "struct Particle { float3 pos; float3 vel; };\n"
    "RWStructuredBuffer<Particle> parts : register(u0);\n"
    "cbuffer CB : register(b0) { float dt; float time; float2 pad; };\n"
    "[numthreads(256,1,1)]\n"
    "void CSMain(uint3 id : SV_DispatchThreadID) {\n"
    "  Particle p = parts[id.x];\n"
    "  float3 toC = -p.pos; float d = length(toC) + 0.001;\n"
    "  float3 grav = toC/d * (3.0/(d*d+0.5));\n"
    "  float3 tang = cross(float3(0,1,0), p.pos);\n"
    "  p.vel += (grav + tang*0.25) * dt; p.vel *= 0.999;\n"
    "  p.pos += p.vel * dt;\n"
    "  if (length(p.pos) > 32.0) { p.pos *= 0.03; p.vel *= 0.2; }\n"
    "  parts[id.x] = p; }\n";

static const char *kCompVS =
    "struct Particle { float3 pos; float3 vel; };\n"
    "StructuredBuffer<Particle> parts : register(t0);\n"
    "cbuffer CB : register(b0) { row_major float4x4 mvp; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float3 col : COLOR; };\n"
    "VSOut VSMain(uint vid : SV_VertexID) {\n"
    "  Particle p = parts[vid]; VSOut o; o.pos = mul(float4(p.pos,1.0), mvp);\n"
    "  float sp = saturate(length(p.vel) * 0.25);\n"
    "  o.col = lerp(float3(0.15,0.35,1.0), float3(1.0,0.55,0.1), sp); return o; }\n"
    "float4 PSMain(VSOut i) : SV_TARGET { return float4(i.col, 1.0); }\n";

typedef struct {
    float pos[3];
    float vel[3];
} Particle;

typedef struct {
    float dt, time, pad[2];
} CompCB;

static struct {
    ID3D11ComputeShader *cs;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11Buffer *buf, *cscb, *vscb;
    ID3D11UnorderedAccessView *uav;
    ID3D11ShaderResourceView *srv;
} g_comp;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static int comp_init(ID3D11Device *dev, ID3D11DeviceContext *ctx, int w, int h) {
    (void)ctx;
    (void)w;
    (void)h;
    ID3DBlob *csb = compile_hlsl(kCompCS, "CSMain", "cs_5_0");
    ID3DBlob *vsb = compile_hlsl(kCompVS, "VSMain", "vs_5_0");
    ID3DBlob *psb = compile_hlsl(kCompVS, "PSMain", "ps_5_0");
    if (!csb || !vsb || !psb) return 1;
    ID3D11Device_CreateComputeShader(dev, ID3D10Blob_GetBufferPointer(csb),
                                     ID3D10Blob_GetBufferSize(csb), NULL, &g_comp.cs);
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_comp.vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_comp.ps);
    ID3D10Blob_Release(csb);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);
    if (!g_comp.cs) {
        fail_box("Compute shaders (cs_5_0) are not available on this D3D11 device.");
        return 1;
    }

    Particle *init = (Particle *)malloc(sizeof(Particle) * PART_COUNT);
    if (!init) return 1;
    srand(1234);
    for (int i = 0; i < PART_COUNT; i++) {
        float r = 4.0f + frand() * 8.0f;
        float th = frand() * 2.0f * PIF, ph = (frand() - 0.5f) * PIF;
        init[i].pos[0] = r * cosf(ph) * cosf(th);
        init[i].pos[1] = r * sinf(ph);
        init[i].pos[2] = r * cosf(ph) * sinf(th);
        // tangential initial velocity (swirl)
        init[i].vel[0] = -init[i].pos[2] * 0.15f;
        init[i].vel[1] = (frand() - 0.5f) * 0.4f;
        init[i].vel[2] = init[i].pos[0] * 0.15f;
    }
    D3D11_BUFFER_DESC bd;
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = sizeof(Particle) * PART_COUNT;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Particle);
    D3D11_SUBRESOURCE_DATA sr;
    memset(&sr, 0, sizeof(sr));
    sr.pSysMem = init;
    ID3D11Device_CreateBuffer(dev, &bd, &sr, &g_comp.buf);
    free(init);

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    memset(&ud, 0, sizeof(ud));
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = PART_COUNT;
    ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)g_comp.buf, &ud, &g_comp.uav);

    D3D11_SHADER_RESOURCE_VIEW_DESC sd;
    memset(&sd, 0, sizeof(sd));
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = PART_COUNT;
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)g_comp.buf, &sd, &g_comp.srv);

    D3D11_BUFFER_DESC cbd;
    memset(&cbd, 0, sizeof(cbd));
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbd.ByteWidth = sizeof(CompCB);
    ID3D11Device_CreateBuffer(dev, &cbd, NULL, &g_comp.cscb);
    cbd.ByteWidth = sizeof(Mat4);
    ID3D11Device_CreateBuffer(dev, &cbd, NULL, &g_comp.vscb);
    return (g_comp.uav && g_comp.srv) ? 0 : 1;
}

static void comp_frame(ID3D11DeviceContext *ctx, double t, float aspect) {
    // Advance the simulation (fixed dt for stability).
    CompCB cb = {0.016f, (float)t, {0, 0}};
    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_comp.cscb, 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, &cb, sizeof(cb));
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_comp.cscb, 0);
    }
    ID3D11DeviceContext_CSSetShader(ctx, g_comp.cs, NULL, 0);
    ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 1, &g_comp.cscb);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &g_comp.uav, NULL);
    ID3D11DeviceContext_Dispatch(ctx, PART_GROUPS, 1, 1);
    // Unbind UAV + compute shader before reading the buffer as an SRV.
    ID3D11UnorderedAccessView *nuav = NULL;
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 1, &nuav, NULL);
    ID3D11DeviceContext_CSSetShader(ctx, NULL, NULL, 0);

    Mat4 world = mat_mul(mat_rotate(0, 1, 0, (float)t * 0.2f), mat_rotate(1, 0, 0, 0.35f));
    Mat4 mvp = mat_mul(mat_mul(world, mat_translate(0, 0, -70)),
                       mat_perspective(0.9f, aspect, 0.1f, 300.0f));
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_comp.vscb, 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, mvp.m, sizeof(mvp.m));
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_comp.vscb, 0);
    }
    ID3D11DeviceContext_IASetInputLayout(ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext_VSSetShader(ctx, g_comp.vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &g_comp.vscb);
    ID3D11DeviceContext_VSSetShaderResources(ctx, 0, 1, &g_comp.srv);
    ID3D11DeviceContext_PSSetShader(ctx, g_comp.ps, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, PART_COUNT, 0);
    // Unbind the SRV so next frame's compute can take the UAV.
    ID3D11ShaderResourceView *nsrv = NULL;
    ID3D11DeviceContext_VSSetShaderResources(ctx, 0, 1, &nsrv);
}

static void comp_cleanup(void) {
    if (g_comp.vscb) ID3D11Buffer_Release(g_comp.vscb);
    if (g_comp.cscb) ID3D11Buffer_Release(g_comp.cscb);
    if (g_comp.srv) ID3D11ShaderResourceView_Release(g_comp.srv);
    if (g_comp.uav) ID3D11UnorderedAccessView_Release(g_comp.uav);
    if (g_comp.buf) ID3D11Buffer_Release(g_comp.buf);
    if (g_comp.ps) ID3D11PixelShader_Release(g_comp.ps);
    if (g_comp.vs) ID3D11VertexShader_Release(g_comp.vs);
    if (g_comp.cs) ID3D11ComputeShader_Release(g_comp.cs);
    memset(&g_comp, 0, sizeof(g_comp));
}

// ============================== DOLPHIN scene ===============================
// The classic DolphinVS underwater scene, reproduced from the original Microsoft
// DirectX SDK assets (embedded in dolphin_assets.h): the real 284-vertex dolphin
// mesh tweened between its 3 keyframe poses (Dolphin1/2/3.x) for the swim, its
// skin texture, the seafloor mesh + texture, and the 32-frame animated caustics,
// with underwater fog. The 3-keyframe position+normal tween reproduces the
// original DolphinTween.vsh technique.
static const char *kDolBodyHLSL =
    "cbuffer CB:register(b0){row_major float4x4 mvp;float4 weights;float4 lightdir;float4 fog;};\n"
    "Texture2D dtex:register(t0); SamplerState smp:register(s0);\n"
    "struct VSIn{float3 p0:POSITION0;float3 p1:POSITION1;float3 p2:POSITION2;"
    "float3 n0:NORMAL0;float3 n1:NORMAL1;float3 n2:NORMAL2;float2 uv:TEXCOORD0;};\n"
    "struct VSOut{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;float3 nrm:NORMAL;float fog:FOG;};\n"
    "VSOut VSMain(VSIn i){\n"
    "  float3 p = i.p0*weights.x + i.p1*weights.y + i.p2*weights.z;\n"
    "  float3 n = i.n0*weights.x + i.n1*weights.y + i.n2*weights.z;\n"
    "  VSOut o; o.pos = mul(float4(p,1.0),mvp); o.uv=i.uv; o.nrm=n;\n"
    "  o.fog = saturate((o.pos.w - fog.x)/(fog.y - fog.x)); return o; }\n"
    "float4 PSMain(VSOut i):SV_TARGET{\n"
    "  float3 N=normalize(i.nrm); float3 L=normalize(lightdir.xyz);\n"
    "  float d = saturate(dot(N,L))*0.75 + 0.45;\n"
    "  float3 c = dtex.Sample(smp,i.uv).rgb * d;\n"
    "  c = lerp(c, float3(0.10,0.32,0.45), i.fog); return float4(c,1.0); }\n";

static const char *kSeaHLSL =
    "cbuffer CB:register(b0){row_major float4x4 mvp;float4 weights;float4 lightdir;float4 fog;};\n"
    "Texture2D stex:register(t0); Texture2DArray ctex:register(t1); SamplerState smp:register(s0);\n"
    "struct VSIn{float3 pos:POSITION0;float3 nrm:NORMAL0;float2 uv:TEXCOORD0;};\n"
    "struct VSOut{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;float fog:FOG;};\n"
    "VSOut VSMain(VSIn i){VSOut o; o.pos=mul(float4(i.pos,1.0),mvp); o.uv=i.uv;\n"
    "  o.fog=saturate((o.pos.w - fog.x)/(fog.y - fog.x)); return o; }\n"
    "float4 PSMain(VSOut i):SV_TARGET{\n"
    "  float3 base = stex.Sample(smp,i.uv).rgb;\n"
    "  float caus = ctex.Sample(smp, float3(i.uv*3.0, fog.z)).r;\n"
    "  float3 c = base*(0.55 + caus*1.1);\n"
    "  c = lerp(c, float3(0.10,0.32,0.45), i.fog); return float4(c,1.0); }\n";

typedef struct {
    Mat4 mvp;
    float weights[4];
    float light[4];
    float fog[4];  // x=start y=end z=caust-frame w=time
} DolSceneCB;

static struct {
    ID3D11VertexShader *dvs, *svs;
    ID3D11PixelShader *dps, *sps;
    ID3D11InputLayout *dlayout, *slayout;
    ID3D11Buffer *dvbo, *dibo, *svbo, *sibo, *cbo;
    ID3D11Texture2D *dtex, *stex, *ctex;
    ID3D11ShaderResourceView *dsrv, *ssrv, *csrv;
    ID3D11SamplerState *smp;
    ID3D11RasterizerState *rs;
} g_dol;

static void dol_upload(ID3D11DeviceContext *ctx, const DolSceneCB *cb) {
    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_dol.cbo, 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        memcpy(map.pData, cb, sizeof(*cb));
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_dol.cbo, 0);
    }
}

static int dol_init(ID3D11Device *dev, ID3D11DeviceContext *ctx, int w, int h) {
    (void)ctx;
    (void)w;
    (void)h;
    ID3DBlob *dvb = compile_hlsl(kDolBodyHLSL, "VSMain", "vs_4_0");
    ID3DBlob *dpb = compile_hlsl(kDolBodyHLSL, "PSMain", "ps_4_0");
    ID3DBlob *svb = compile_hlsl(kSeaHLSL, "VSMain", "vs_4_0");
    ID3DBlob *spb = compile_hlsl(kSeaHLSL, "PSMain", "ps_4_0");
    if (!dvb || !dpb || !svb || !spb) return 1;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(dvb),
                                    ID3D10Blob_GetBufferSize(dvb), NULL, &g_dol.dvs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(dpb),
                                   ID3D10Blob_GetBufferSize(dpb), NULL, &g_dol.dps);
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(svb),
                                    ID3D10Blob_GetBufferSize(svb), NULL, &g_dol.svs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(spb),
                                   ID3D10Blob_GetBufferSize(spb), NULL, &g_dol.sps);

    D3D11_INPUT_ELEMENT_DESC dil[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"POSITION", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11Device_CreateInputLayout(dev, dil, 7, ID3D10Blob_GetBufferPointer(dvb),
                                   ID3D10Blob_GetBufferSize(dvb), &g_dol.dlayout);
    D3D11_INPUT_ELEMENT_DESC sil[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11Device_CreateInputLayout(dev, sil, 3, ID3D10Blob_GetBufferPointer(svb),
                                   ID3D10Blob_GetBufferSize(svb), &g_dol.slayout);
    ID3D10Blob_Release(dvb);
    ID3D10Blob_Release(dpb);
    ID3D10Blob_Release(svb);
    ID3D10Blob_Release(spb);

    // Interleave dolphin vertex buffer: 3 positions + 3 normals + uv = 20 floats.
    float *dv = (float *)malloc(sizeof(float) * 20 * DOLPHIN_NVERTS);
    if (!dv) return 1;
    for (int i = 0; i < DOLPHIN_NVERTS; i++) {
        float *o = &dv[i * 20];
        memcpy(o + 0, &dolphin_pos1[i * 3], 12);
        memcpy(o + 3, &dolphin_pos2[i * 3], 12);
        memcpy(o + 6, &dolphin_pos3[i * 3], 12);
        memcpy(o + 9, &dolphin_nrm1[i * 3], 12);
        memcpy(o + 12, &dolphin_nrm2[i * 3], 12);
        memcpy(o + 15, &dolphin_nrm3[i * 3], 12);
        memcpy(o + 18, &dolphin_uv[i * 2], 8);
    }
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA sr;
    memset(&bd, 0, sizeof(bd));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = (UINT)(sizeof(float) * 20 * DOLPHIN_NVERTS);
    memset(&sr, 0, sizeof(sr));
    sr.pSysMem = dv;
    ID3D11Device_CreateBuffer(dev, &bd, &sr, &g_dol.dvbo);
    free(dv);

    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = sizeof(dolphin_idx);
    sr.pSysMem = dolphin_idx;
    ID3D11Device_CreateBuffer(dev, &bd, &sr, &g_dol.dibo);

    // Seafloor vertex buffer: pos + normal + uv = 8 floats.
    float *sv = (float *)malloc(sizeof(float) * 8 * SEAFLOOR_NVERTS);
    if (!sv) return 1;
    for (int i = 0; i < SEAFLOOR_NVERTS; i++) {
        float *o = &sv[i * 8];
        memcpy(o + 0, &seafloor_pos[i * 3], 12);
        memcpy(o + 3, &seafloor_nrm[i * 3], 12);
        memcpy(o + 6, &seafloor_uv[i * 2], 8);
    }
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = (UINT)(sizeof(float) * 8 * SEAFLOOR_NVERTS);
    sr.pSysMem = sv;
    ID3D11Device_CreateBuffer(dev, &bd, &sr, &g_dol.svbo);
    free(sv);

    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = sizeof(seafloor_idx);
    sr.pSysMem = seafloor_idx;
    ID3D11Device_CreateBuffer(dev, &bd, &sr, &g_dol.sibo);

    // Constant buffer.
    memset(&bd, 0, sizeof(bd));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = sizeof(DolSceneCB);
    ID3D11Device_CreateBuffer(dev, &bd, NULL, &g_dol.cbo);

    // Dolphin + seafloor textures (RGBA8).
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA tsr;
    memset(&td, 0, sizeof(td));
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.Width = DOLTEX_W;
    td.Height = DOLTEX_H;
    memset(&tsr, 0, sizeof(tsr));
    tsr.pSysMem = dolphin_tex;
    tsr.SysMemPitch = DOLTEX_W * 4;
    ID3D11Device_CreateTexture2D(dev, &td, &tsr, &g_dol.dtex);
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)g_dol.dtex, NULL, &g_dol.dsrv);
    td.Width = SEATEX_W;
    td.Height = SEATEX_H;
    tsr.pSysMem = seafloor_tex;
    tsr.SysMemPitch = SEATEX_W * 4;
    ID3D11Device_CreateTexture2D(dev, &td, &tsr, &g_dol.stex);
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)g_dol.stex, NULL, &g_dol.ssrv);

    // Caustic texture array: 32 single-channel (R8) frames.
    D3D11_TEXTURE2D_DESC cd;
    memset(&cd, 0, sizeof(cd));
    cd.Width = CAUST_W;
    cd.Height = CAUST_H;
    cd.MipLevels = 1;
    cd.ArraySize = CAUST_FRAMES;
    cd.Format = DXGI_FORMAT_R8_UNORM;
    cd.SampleDesc.Count = 1;
    cd.Usage = D3D11_USAGE_IMMUTABLE;
    cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA csr[CAUST_FRAMES];
    for (int i = 0; i < CAUST_FRAMES; i++) {
        csr[i].pSysMem = &caust_tex[i * CAUST_W * CAUST_H];
        csr[i].SysMemPitch = CAUST_W;
        csr[i].SysMemSlicePitch = 0;
    }
    ID3D11Device_CreateTexture2D(dev, &cd, csr, &g_dol.ctex);
    D3D11_SHADER_RESOURCE_VIEW_DESC cv;
    memset(&cv, 0, sizeof(cv));
    cv.Format = DXGI_FORMAT_R8_UNORM;
    cv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    cv.Texture2DArray.MipLevels = 1;
    cv.Texture2DArray.ArraySize = CAUST_FRAMES;
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)g_dol.ctex, &cv, &g_dol.csrv);

    D3D11_SAMPLER_DESC sd;
    memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11Device_CreateSamplerState(dev, &sd, &g_dol.smp);

    D3D11_RASTERIZER_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // mesh winding not guaranteed; show both sides
    rd.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState(dev, &rd, &g_dol.rs);
    return (g_dol.dsrv && g_dol.ssrv && g_dol.csrv) ? 0 : 1;
}

static void dol_frame(ID3D11DeviceContext *ctx, double t, float aspect) {
    // Orbiting camera (object fixed in world -> lighting stays put).
    float ang = (float)t * 0.3f, R = 7.0f;
    Mat4 view = mat_lookat(R * sinf(ang), 2.0f, R * cosf(ang), 0.0f, -0.4f, 0.0f, 0, 1, 0);
    Mat4 vp = mat_mul(view, mat_perspective(0.85f, aspect, 0.1f, 100.0f));

    DolSceneCB cb;
    cb.light[0] = 0.3f; cb.light[1] = 1.0f; cb.light[2] = 0.4f; cb.light[3] = 0.0f;
    cb.fog[0] = 6.0f; cb.fog[1] = 16.0f;
    cb.fog[2] = (float)(((int)(t * 15.0)) % CAUST_FRAMES);
    cb.fog[3] = (float)t;

    ID3D11DeviceContext_RSSetState(ctx, g_dol.rs);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // --- Seafloor ---
    Mat4 m_sea = mat_mul(mat_scale(0.04f), mat_translate(0.0f, -2.5f, 0.0f));
    cb.mvp = mat_mul(m_sea, vp);
    cb.weights[0] = cb.weights[1] = cb.weights[2] = cb.weights[3] = 0.0f;
    dol_upload(ctx, &cb);
    UINT ss = sizeof(float) * 8, so = 0;
    ID3D11DeviceContext_IASetInputLayout(ctx, g_dol.slayout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &g_dol.svbo, &ss, &so);
    ID3D11DeviceContext_IASetIndexBuffer(ctx, g_dol.sibo, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_VSSetShader(ctx, g_dol.svs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &g_dol.cbo);
    ID3D11DeviceContext_PSSetShader(ctx, g_dol.sps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &g_dol.cbo);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &g_dol.smp);
    ID3D11ShaderResourceView *seasrv[2] = {g_dol.ssrv, g_dol.csrv};
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 2, seasrv);
    ID3D11DeviceContext_DrawIndexed(ctx, SEAFLOOR_NINDICES, 0, 0);

    // --- Dolphin (3-keyframe swim tween) ---
    Mat4 m_dol = mat_mul(mat_scale(0.01f), mat_translate(-0.25f, 0.27f, 0.0f));
    cb.mvp = mat_mul(m_dol, vp);
    float phase = fmodf((float)t * 1.6f, 3.0f);
    int seg = (int)phase;
    float frac = phase - (float)seg;
    cb.weights[0] = cb.weights[1] = cb.weights[2] = 0.0f;
    cb.weights[seg] = 1.0f - frac;
    cb.weights[(seg + 1) % 3] = frac;
    dol_upload(ctx, &cb);
    UINT ds = sizeof(float) * 20, dofs = 0;
    ID3D11DeviceContext_IASetInputLayout(ctx, g_dol.dlayout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &g_dol.dvbo, &ds, &dofs);
    ID3D11DeviceContext_IASetIndexBuffer(ctx, g_dol.dibo, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_VSSetShader(ctx, g_dol.dvs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, g_dol.dps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &g_dol.dsrv);
    ID3D11DeviceContext_DrawIndexed(ctx, DOLPHIN_NINDICES, 0, 0);
}

static void dol_cleanup(void) {
    if (g_dol.rs) ID3D11RasterizerState_Release(g_dol.rs);
    if (g_dol.smp) ID3D11SamplerState_Release(g_dol.smp);
    if (g_dol.csrv) ID3D11ShaderResourceView_Release(g_dol.csrv);
    if (g_dol.ssrv) ID3D11ShaderResourceView_Release(g_dol.ssrv);
    if (g_dol.dsrv) ID3D11ShaderResourceView_Release(g_dol.dsrv);
    if (g_dol.ctex) ID3D11Texture2D_Release(g_dol.ctex);
    if (g_dol.stex) ID3D11Texture2D_Release(g_dol.stex);
    if (g_dol.dtex) ID3D11Texture2D_Release(g_dol.dtex);
    if (g_dol.cbo) ID3D11Buffer_Release(g_dol.cbo);
    if (g_dol.sibo) ID3D11Buffer_Release(g_dol.sibo);
    if (g_dol.svbo) ID3D11Buffer_Release(g_dol.svbo);
    if (g_dol.dibo) ID3D11Buffer_Release(g_dol.dibo);
    if (g_dol.dvbo) ID3D11Buffer_Release(g_dol.dvbo);
    if (g_dol.slayout) ID3D11InputLayout_Release(g_dol.slayout);
    if (g_dol.dlayout) ID3D11InputLayout_Release(g_dol.dlayout);
    if (g_dol.sps) ID3D11PixelShader_Release(g_dol.sps);
    if (g_dol.svs) ID3D11VertexShader_Release(g_dol.svs);
    if (g_dol.dps) ID3D11PixelShader_Release(g_dol.dps);
    if (g_dol.dvs) ID3D11VertexShader_Release(g_dol.dvs);
    memset(&g_dol, 0, sizeof(g_dol));
}

// ============================== scene registry ==============================
static const D3D11Scene kScenes[] = {
    {"spin", "D3D11 Cube", spin_init, spin_frame, spin_cleanup},
    {"textured", "D3D11 Textured", tex_init, tex_frame, tex_cleanup},
    {"instanced", "D3D11 Instanced", inst_init, inst_frame, inst_cleanup},
    {"tess", "D3D11 Tessellation", tess_init, tess_frame, tess_cleanup},
    {"compute", "D3D11 Compute Particles", comp_init, comp_frame, comp_cleanup},
    {"dolphin", "D3D11 Dolphin", dol_init, dol_frame, dol_cleanup},
};

static const D3D11Scene *pick_scene(const char *name) {
    if (name)
        for (size_t i = 0; i < sizeof(kScenes) / sizeof(kScenes[0]); i++)
            if (strcmp(kScenes[i].name, name) == 0) return &kScenes[i];
    return &kScenes[0];  // default: spin
}

// ================================ the runner ================================
int aio_run_d3d11_cube(HINSTANCE hinst, const char *scene_name) {
    const D3D11Scene *scene = pick_scene(scene_name);
    const char *api = scene->label;
    const char *cls = "AIOD3D11Cube";

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = d3d11_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hinst, MAKEINTRESOURCEA(1));
    wc.lpszClassName = cls;
    RegisterClassA(&wc);

    char wtitle[96];
    snprintf(wtitle, sizeof(wtitle), "AIO Graphics Test  -  %s", api);
    HWND hwnd = CreateWindowA(cls, wtitle, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                              CW_USEDEFAULT, 640, 480, NULL, NULL, hinst, NULL);
    if (!hwnd) return 1;

    RECT rc;
    GetClientRect(hwnd, &rc);
    g_w = rc.right - rc.left;
    g_h = rc.bottom - rc.top;
    if (g_w <= 0) g_w = 640;
    if (g_h <= 0) g_h = 480;

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

    ID3D11Texture2D *backbuf = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&backbuf);
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)backbuf, NULL, &rtv);

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

    // Shader compiler (runtime, dynamic).
    HMODULE d3dc = LoadLibraryA("d3dcompiler_47.dll");
    if (!d3dc) d3dc = LoadLibraryA("d3dcompiler_43.dll");
    g_compile = d3dc ? (PFN_D3DCompile)GetProcAddress(d3dc, "D3DCompile") : NULL;
    if (!g_compile) {
        fail_box(
            "Could not load d3dcompiler (D3DCompile) in this container.\n\n"
            "The HLSL shaders for the Direct3D 11 scene can't be compiled.");
        DestroyWindow(hwnd);
        return 1;
    }

    if (scene->init(dev, ctx, g_w, g_h) != 0) {
        DestroyWindow(hwnd);
        return 1;
    }

    aio_hud_create(hinst);
    char hud0[96];
    snprintf(hud0, sizeof(hud0), "%s  -  measuring...", api);
    aio_hud_update(hwnd, hud0);

    int bench_on = aio_bench_active();
    LARGE_INTEGER qpf, start, prev;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&start);
    prev = start;

    ULONGLONG last_ms = GetTickCount64();
    uint64_t frames = 0, last_frame = 0;
    const float clear[4] = {0.10f, 0.10f, 0.12f, 1.0f};
    float aspect = (g_h > 0) ? (float)g_w / (float)g_h : 1.0f;

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

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double t = (double)(now.QuadPart - start.QuadPart) / (double)qpf.QuadPart;

        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear);
        ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        scene->frame(ctx, t, aspect);
        IDXGISwapChain_Present(swap, 0, 0);
        frames++;

        if (bench_on) {
            double dt_ms = (double)(now.QuadPart - prev.QuadPart) * 1000.0 / (double)qpf.QuadPart;
            aio_bench_add(dt_ms);
            prev = now;
            if (t >= (double)aio_bench_seconds()) g_quit = 1;
        }

        ULONGLONG now_ms = GetTickCount64();
        if (now_ms - last_ms >= 500) {
            double secs = (double)(now_ms - last_ms) / 1000.0;
            double fps = (secs > 0.0) ? (double)(frames - last_frame) / secs : 0.0;
            char hud[96], title[160];
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
        QueryPerformanceCounter(&prev);
        double total = (double)(prev.QuadPart - start.QuadPart) / (double)qpf.QuadPart;
        // Keyed by the per-scene label so each DX11 scene gets its own result file.
        char *res = aio_bench_finish(api, total);
        if (res) {
            MessageBoxA(NULL, res, "AIO Graphics Test - Benchmark", MB_OK | MB_ICONINFORMATION);
            free(res);
        }
    }

    aio_hud_destroy();
    scene->cleanup();

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
