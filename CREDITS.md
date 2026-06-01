# Credits & Attribution

## Authors

- [**The412Banner**](https://github.com/The412Banner) — author / development.
- [**Nick (@Xnick417x)**](https://github.com/Xnick417x) — **co-author**: the idea,
  inspiration, and ongoing help behind the project. Thank you, Nick. 🙏

AIO Graphics Test stands on the shoulders of open-source graphics projects and was
informed by the `3d-tests` reference kit. **No third-party `.exe` binaries are bundled** —
where a tool is listed as "reference", its functionality is reimplemented natively in our
own code. The one exception is the **DolphinVS** scene, which embeds the original Microsoft
DirectX SDK sample *data* (meshes, textures, caustics) — see the dedicated section below.

## Code we build on (included in this repo)

- **Khronos `vkcube` / Vulkan-Tools** — our `src/cube.c` is a fork of `cube/cube.c`
  (tag `sdk-1.3.239.0`), and we include its `cube.vert`, `cube.frag`, `linmath.h`,
  `gettime.h`, `object_type_string_helper.h`, and `lunarg.ppm.h` (the LunarG texture).
  © 2015–2019 The Khronos Group Inc., Valve Corporation, LunarG, Inc.
  Licensed under **Apache-2.0** (see the header in `src/cube.c` and `LICENSE`).
  https://github.com/KhronosGroup/Vulkan-Tools

## Build-time dependencies (fetched in CI, not vendored)

- **Vulkan-Headers**, **Vulkan-Loader** — © The Khronos Group, Apache-2.0.
- **glslang** — Khronos reference GLSL→SPIR-V compiler, Apache-2.0 / BSD.
- **mingw-w64** — GCC cross-toolchain for the Windows PE target.

## `3d-tests` reference kit (analyzed for behavior; NOT redistributed)

The `/storage/emulated/0/Download/3d-tests/` kit guided which APIs and capabilities
AIO Graphics Test should cover. Credit to their respective authors:

| Demo(s) | API | Source / author |
|---|---|---|
| `vkcube.exe`, `vkcubepp.exe` | Vulkan | Khronos Vulkan-Tools (Apache-2.0) |
| `GPUInfo.exe` | GL + VK info | third-party utility (author unknown); GL+VK reporting reimplemented natively as `--gpuinfo` |
| `DolphinVS.exe` | Direct3D 8 | Microsoft DirectX SDK sample (© Microsoft) |
| `CubeMap.exe`, `SphereMap.exe` (EnvMapping) | Direct3D 9 | Microsoft DirectX SDK samples (© Microsoft) |
| `D3D12HelloTriangle.exe` | Direct3D 12 | Microsoft DirectX-Graphics-Samples (MIT) |
| `D3D9_*`, `D3D11_*`, `D3D12_*` test exes | D3D9/11/12 | third-party DirectX test demos |
| `font.exe`, `skyfly.exe`, `wave.exe` | OpenGL | classic OpenGL demos (SGI / Mesa heritage) |

## DolphinVS assets (embedded)

The **Dolphin** scene reproduces the classic Microsoft *DolphinVS* DirectX SDK sample. Its
original assets — the dolphin mesh (3 keyframe poses), the seafloor mesh, the skin and
seafloor textures, and the 32-frame caustic animation — are parsed by
`tools/gen_dolphin_assets.py` into `src/dolphin_assets.h` and **embedded in the binary**.
The renderer (the 3-keyframe tween, lighting, caustics) is our own code; the asset *data* is
Microsoft's.

- **DolphinVS sample assets** — © **Microsoft Corporation**, from the DirectX SDK. Included
  for the homage scene only. Not affiliated with or endorsed by Microsoft.

Trademarks (DirectX, Direct3D, Windows, Vulkan, OpenGL) belong to their respective owners.
AIO Graphics Test is an independent tool and is not affiliated with or endorsed by them.
