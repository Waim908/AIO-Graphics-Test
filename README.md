# AIO-Cube

An all-in-one graphics diagnostic + benchmark cube for testing the **star / Winlator**
Wine graphics stack on Android (DXVK, VKD3D, Turnip, Zink). One Windows `.exe` you drop
into a container — it replaces both `vkcube.exe` and `GPUInfo.exe`.

Forked from Khronos [Vulkan-Tools `vkcube`](https://github.com/KhronosGroup/Vulkan-Tools)
(`cube.c`, tag `sdk-1.3.239.0`, Apache-2.0 — see header in `src/cube.c`). The
self-contained pre-codegen base links `vulkan-1` directly (no `cube_functions.h`
generation step).

## Modes

| Flag | What it does | Replaces |
|------|--------------|----------|
| *(default)* | Renders the spinning cube — smoke test | `vkcube.exe` |
| `--gpuinfo` / `--report` | Dumps **OpenGL** (`GL_VENDOR/RENDERER/VERSION/GLSL/EXTENSIONS` via a throwaway WGL context) **and Vulkan** (device, driver + API version, memory heaps, queue families, features, instance/device extensions) to console + `AIO-Cube_report.txt` | `GPUInfo.exe` |
| `--bench <sec>` | Times the run, reports avg / min / max / 1%-low FPS, writes a CSV | — |
| `--api vk\|d3d11\|d3d12` | Selects render backend so one binary exercises native VK (Turnip), DXVK (d3d11), VKD3D (d3d12) | — |
| `--semaphore timeline\|binary` | Forces the semaphore path to probe/measure the DXVK 2.4.1-vs-2.5+ Turnip-kgsl half-FPS regression | — |
| `--c <N>` | Run N frames then exit (inherited from vkcube; used by bench) | |

All report/CSV output goes to a file as well as stdout, so results survive the
PRoot/Termux session getting OOM-killed mid-test (CMA exhaustion on the device).

## Build

CI only (no local builds). Cross-compiled Linux → Windows x86_64 PE on GitHub Actions:
`.github/workflows/build-windows.yml` (mingw-w64 + Vulkan-Headers + cross-built
Vulkan-Loader import lib + glslang → `AIO-Cube.exe`). Run the **Build AIO-Cube
(Windows PE)** workflow; grab the `AIO-Cube-windows-x86_64` artifact.

## Roadmap

- **v0.1** — base cube cross-compiles to `AIO-Cube.exe` (prove the pipeline). ← current
- **v0.2** — `--gpuinfo` (GL + VK dump, the `GPUInfo.exe` replacement) + caps report file.
- **v0.3** — `--bench` FPS/frametime/1%-low + CSV.
- **v0.4** — D3D11 (DXVK) + D3D12 (VKD3D) backends via `--api`.
- **v0.5** — shared on-screen HUD (FPS + driver string) + `--semaphore` regression probe.

## Layout

```
src/cube.c            forked vkcube (renamed APP_SHORT_NAME -> "AIO-Cube")
src/cube.vert/.frag   GLSL shaders (compiled to *.inc SPIR-V headers in CI)
src/*.h               linmath, gettime, object_type_string_helper, lunarg.ppm (texture)
cmake/                mingw-w64 cross toolchain file
```

## License

Apache-2.0 (inherited from Vulkan-Tools). The third-party `GPUInfo.exe` is **not**
bundled or copied — its GL+VK reporting is reimplemented natively here.
