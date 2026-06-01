# AIO Graphics Test

An all-in-one graphics **diagnostic + benchmark** tool for the **Winlator/Gamehub/GameNative** Wine
graphics stack on Android (DXVK, VKD3D-Proton, Turnip, Zink). One Windows `.exe` you drop
into a container — it replaces `vkcube.exe`, `GPUInfo.exe`, and the whole `3d-tests` kit
(DX8/9/11/12 + GL demos) with a single, touch-friendly, instrumented binary that renders
the **same cube through every graphics API** so you can see exactly which translation layer
is broken or slow.

Forked from Khronos [Vulkan-Tools `vkcube`](https://github.com/KhronosGroup/Vulkan-Tools)
(`cube.c`, tag `sdk-1.3.239.0`, Apache-2.0). The self-contained pre-codegen base links
`vulkan-1` directly (no `cube_functions.h` generation step).

> **Idea & inspiration:** [**Nick (@Xnick417x)**](https://github.com/Xnick417x) — co-author. See [Credits](#credits).
>
> **▶ Showcase video:** https://youtu.be/gKhwjwjGvWI

## Download

Grab the latest `AIO-Graphics-Test.exe` from the
[**Releases**](https://github.com/The412Banner/AIO-Graphics-Test/releases) page, drop it
into a Winlator container, and run it — it opens a menu, no install. See it in action in the
[**showcase video**](https://youtu.be/gKhwjwjGvWI). The container already
provides `vulkan-1.dll` (every Winlator container does). The exe's embedded cube icon is
used as the shortcut art when you add it to a container.

## What it does

A single binary, driven by an **in-app menu** (touch-friendly — Winlator is a touchscreen),
with optional CLI shortcuts:

- **App shell** — a persistent left-sidebar menu + content pane. Pick a test; GPU Info opens
  in-frame, cube tests open in their own window (so the menu stays usable).
- **GPU / driver report** — an in-frame tabbed **Vulkan / OpenGL** view (device, driver +
  API version, memory heaps, queue families, features, extensions). Replaces `GPUInfo.exe`.
- **Multi-API renderer** — the same cube through **six** graphics APIs, all device-confirmed
  on Adreno 750 / Turnip:

  | Backend | Path it exercises |
  |---|---|
  | **Vulkan**     | native Vulkan → **Turnip** (no translation — the baseline) |
  | **OpenGL**     | OpenGL → **Zink / wined3d** → Vulkan |
  | **Direct3D 8** | **DXVK** d3d8 → d3d9 → Vulkan |
  | **Direct3D 9** | **DXVK** d3d9 → Vulkan |
  | **Direct3D 11**| **DXVK** d3d11 → Vulkan |
  | **Direct3D 12**| **VKD3D-Proton** d3d12 → Vulkan |

- **DX11 test suite** — Direct3D 11 is six scenes, each stressing a different part of DXVK:

  | Scene | Exercises |
  |---|---|
  | Spinning cube | baseline pipeline |
  | Textured cube | texture upload + SRV + sampler |
  | Instanced (512 cubes) | instanced draw throughput |
  | Tessellation | hull/domain shaders (feature level 11) |
  | Compute particles | the D3D11 compute path (UAV/SRV) |
  | **Dolphin** | the classic **DolphinVS** underwater scene — the real mesh tweened across 3 keyframes, seafloor + 32-frame animated caustics (see [Credits](#credits)) |

- **Benchmark** — a view (or `--bench <sec>`) that times a run and reports **Avg / Min / Max**
  FPS next to each row (full avg/min/max/1%-low in a popup + a per-frame **CSV**):
  - **Selectable length** — 15 / 30 / 45 / 60 s.
  - **Vsync toggle** — off (uncapped, for true throughput) or on.
  - **Run All** — one tap sweeps every API/scene **sequentially and hands-free**: each
    result popup auto-closes after 3 s so it proceeds without clicking (a single manual
    benchmark keeps its popup until you dismiss it). No GPU contention between tests.
  - Results are **cached for the session**, so they stay visible when you switch views.
- **Semaphore Probe** — benchmarks the instanced D3D11 cube with **timeline vs binary**
  semaphores to measure the Turnip-kgsl timeline-semaphore regression, and prints a plain
  verdict (e.g. *"binary is 1.7× faster"*). (The binary path only differs on a DXVK build
  that honors `DXVK_DISABLE_TIMELINE_SEMAPHORES`.)
- **Live HUD** — every cube window shows the active API + live FPS as an on-screen overlay.
- **Hang watchdog** — if a backend deadlocks (e.g. a broken GL stack blocking in
  `SwapBuffers`), it self-closes after ~12 s instead of locking up the container.
- **Robust by design** — DXVK / VKD3D / d3dcompiler are all loaded **dynamically**, so the
  exe launches and shows a graceful notice even on a container that lacks them. All output is
  written to disk too, so results survive a PRoot/Termux OOM-kill mid-test.

## CLI shortcuts

The menu is the primary interface, but every mode has a flag too:

| Flag | What it does |
|------|--------------|
| *(default)* | Opens the app shell (menu) |
| `--gpuinfo` / `--report` | Dump GL + VK adapter info to console + `AIO-Graphics-Test_report.txt`, then exit |
| `--cube vk\|gl\|dx8\|dx9\|dx11\|dx12` | Launch a backend directly in its own window |
| `--cube dx11 --scene spin\|textured\|instanced\|tess\|compute\|dolphin` | Pick a DX11 scene |
| `--bench <sec>` | Run the launched cube as a timed benchmark (avg/min/max/1%-low + CSV) |
| `--vsync` | Present with vsync (default is uncapped) |
| `--autoclose <sec>` | Auto-dismiss the benchmark result popup after N s (Run All uses 3) |
| `--semaphore timeline\|binary` | Force the DXVK semaphore path (the probe uses this) |
| `--no-menu` | Skip the shell and launch the cube directly |

## Build

CI only (no local builds). Cross-compiled Linux → Windows x86_64 PE on GitHub Actions
(`.github/workflows/build-windows.yml`): mingw-w64 + Vulkan-Headers + a cross-built
Vulkan-Loader import lib + glslang + `windres` (icon) → `AIO-Graphics-Test.exe`. Releases are
the exact CI-built artifact. Only `vulkan-1` is statically imported (always present in a
container); d3d8/9/11/12, dxgi, and d3dcompiler are loaded at runtime.

## Layout

```
src/cube.c            forked vkcube + WinMain dispatch (also the Vulkan backend)
src/menu.c            app shell: sidebar menu, GPU Info tabs, scene/benchmark/probe views
src/cube_gl.c         OpenGL backend (WGL)
src/cube_d3d8.c       Direct3D 8 backend (DXVK d3d8 wrapper)
src/cube_d3d9.c       Direct3D 9 backend (DXVK)
src/cube_d3d11.c      Direct3D 11 scene framework + 6 scenes
src/cube_d3d12.c      Direct3D 12 backend (VKD3D-Proton)
src/gpuinfo.c         GL + VK adapter report
src/bench.c           benchmark instrumentation (avg/min/max/1%-low + CSV, vsync flag)
src/hud.c             in-window FPS/API overlay
src/watchdog.c        render-loop hang watchdog
src/dolphin_assets.h  embedded DolphinVS assets (generated)
src/app.rc, app.ico   app icon (PE resource; also the Winlator shortcut art)
tools/gen_dolphin_assets.py   one-time .x/.bmp/.tga → dolphin_assets.h
tools/gen_icon.sh             regenerate app.ico (ImageMagick)
cmake/                mingw-w64 cross toolchain file
```

## Credits

**AIO Graphics Test** is built by [**The412Banner**](https://github.com/The412Banner) with
co-author [**Nick (@Xnick417x)**](https://github.com/Xnick417x) — the **idea, inspiration,
and ongoing help** behind the project. Thank you, Nick. 🙏

It stands on open-source graphics work and was informed by the `3d-tests` reference kit:

- **Khronos `vkcube` / Vulkan-Tools** — `src/cube.c` is a fork of `cube/cube.c`
  (tag `sdk-1.3.239.0`), Apache-2.0. © The Khronos Group, Valve, LunarG.
- **Vulkan-Headers / Vulkan-Loader / glslang** — Khronos, Apache-2.0 (fetched in CI).
- **DXVK** and **VKD3D-Proton** — the translation layers this tool exercises (not bundled).

**DolphinVS assets** — the Dolphin scene embeds the original Microsoft *DolphinVS* DirectX
SDK sample data (dolphin + seafloor meshes, textures, and the 32-frame caustics), parsed
into `src/dolphin_assets.h`. Those assets are **© Microsoft Corporation**, included for the
homage scene; all trademarks belong to their owners. This project is independent and not
affiliated with or endorsed by Microsoft.

Full attribution is in [`CREDITS.md`](CREDITS.md). Everything else is reimplemented natively
— no third-party `.exe` binaries are bundled.

## License

Apache-2.0 (inherited from Vulkan-Tools) — see [`LICENSE`](LICENSE).
