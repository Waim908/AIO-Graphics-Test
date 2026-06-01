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
    float a = (float)t * 0.9f;
    Mat4 model = mat_mul(mat_rotate(1, 1, 0, a), mat_rotate(0, 1, 0, a * 0.5f));
    Mat4 mvp = mat_mul(mat_mul(model, mat_translate(0, 0, -5)),
                       mat_perspective(0.7854f, aspect, 0.1f, 100.0f));
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
    float a = (float)t * 0.9f;
    Mat4 model = mat_mul(mat_rotate(1, 1, 0, a), mat_rotate(0, 1, 0, a * 0.5f));
    Mat4 mvp = mat_mul(mat_mul(model, mat_translate(0, 0, -5)),
                       mat_perspective(0.7854f, aspect, 0.1f, 100.0f));
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

// ============================== scene registry ==============================
static const D3D11Scene kScenes[] = {
    {"spin", "D3D11 Cube", spin_init, spin_frame, spin_cleanup},
    {"textured", "D3D11 Textured", tex_init, tex_frame, tex_cleanup},
    {"instanced", "D3D11 Instanced", inst_init, inst_frame, inst_cleanup},
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

    if (bench_on) {
        QueryPerformanceCounter(&prev);
        double total = (double)(prev.QuadPart - start.QuadPart) / (double)qpf.QuadPart;
        // Benchmark result is keyed by a stable label so the shell can find it.
        char *res = aio_bench_finish("Direct3D 11", total);
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
