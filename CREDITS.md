# Credits & Attribution

AIO Graphics Test stands on the shoulders of open-source graphics projects and was
informed by the `3d-tests` reference kit. Note: **no third-party `.exe` binaries are
bundled or redistributed in this repository** — where a tool is listed as "reference",
its functionality is reimplemented natively in our own code.

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

Trademarks (DirectX, Direct3D, Windows, Vulkan, OpenGL) belong to their respective owners.
AIO Graphics Test is an independent tool and is not affiliated with or endorsed by them.
