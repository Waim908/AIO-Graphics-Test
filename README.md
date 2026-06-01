# AIO Graphics Test

An all-in-one graphics diagnostic + benchmark tool for testing the **star / Winlator**
Wine graphics stack on Android (DXVK, VKD3D-Proton, Turnip, Zink). One Windows `.exe` you
drop into a container — it replaces `vkcube.exe`, `GPUInfo.exe`, and the whole `3d-tests`
kit (DX9/11/12 + GL demos) with a single native binary.

Forked from Khronos [Vulkan-Tools `vkcube`](https://github.com/KhronosGroup/Vulkan-Tools)
(`cube.c`, tag `sdk-1.3.239.0`, Apache-2.0 — see header in `src/cube.c`). The
self-contained pre-codegen base links `vulkan-1` directly (no `cube_functions.h`
generation step).

> **Idea & inspiration:** [**Nick (@Xnick417x)**](https://github.com/Xnick417x) — co-author.
> See [Credits](#credits).

> **Full project breakdown:** see [`docs/OVERVIEW.md`](docs/OVERVIEW.md).

## What it is

A single Windows binary that is a **diagnostic + benchmark + multi-API renderer**, driven by
an **in-app menu** (touch-friendly — Winlator is a touchscreen) with optional CLI shortcuts:

- **App shell** — a persistent left-sidebar menu + content pane. Pick a test; it either
  opens in-frame (GPU Info) or launches in its own window (the cube tests).
- **GPU / driver report** — an in-frame tabbed **Vulkan / OpenGL** view (device, driver +
  API version, memory heaps, queue families, features, extensions). Replaces `GPUInfo.exe`.
  Also available as `--gpuinfo` → console + `AIO-Graphics-Test_report.txt`.
- **Multi-API renderer** — the same scene through every graphics API, so one tool pinpoints
  *which translation layer* is broken or slow:

  | Backend | Path it exercises | Status |
  |---|---|---|
  | **Vulkan**  | native Vulkan → **Turnip** (no translation) | ✅ |
  | **OpenGL**  | OpenGL → **Zink / wined3d** → Vulkan | ✅ |
  | **Direct3D 11** | **DXVK** d3d11 → Vulkan | ✅ |
  | **Direct3D 12** | **VKD3D-Proton** d3d12 → Vulkan | ✅ |
  | **Direct3D 9**  | **DXVK** d3d9 → Vulkan | planned |

- **DX11 test suite** — Direct3D 11 isn't just a cube; it's six scenes, each stressing a
  different part of the DXVK path:

  | Scene | Exercises |
  |---|---|
  | Spinning cube | baseline pipeline |
  | Textured cube | texture upload + SRV + sampler |
  | Instanced (512 cubes) | instanced draw throughput |
  | Tessellation | hull/domain shaders (feature level 11) |
  | Compute particles | the D3D11 compute path (UAV/SRV) |
  | **Dolphin** | the classic **DolphinVS** underwater scene — real mesh tweened across 3 keyframes, seafloor + animated caustics (see [Credits](#credits)) |

- **Live HUD** — every cube window shows the active API + live FPS as an on-screen overlay.
- **Benchmark** — an opt-in view (or `--bench <sec>`): runs each API/scene for 15 s and
  reports **Avg / Min / Max** FPS next to each button, plus full avg/min/max/1%-low and a
  per-frame **CSV**. All results are written to disk, so they survive a PRoot/Termux
  OOM-kill mid-test.

## CLI shortcuts

The menu is the primary interface, but every mode has a flag too:

| Flag | What it does |
|------|--------------|
| *(default)* | Opens the app shell (menu) |
| `--gpuinfo` / `--report` | Dump GL + VK adapter info to console + report file, then exit |
| `--cube vk\|gl\|dx11\|dx12` | Launch a cube backend directly in its own window |
| `--cube dx11 --scene spin\|textured\|instanced\|tess\|compute\|dolphin` | Pick a DX11 scene |
| `--bench <sec>` | Run the launched cube as a timed benchmark (avg/min/max/1%-low + CSV) |
| `--no-menu` | Skip the shell and launch the cube directly |

## Build

CI only (no local builds). Cross-compiled Linux → Windows x86_64 PE on GitHub Actions:
`.github/workflows/build-windows.yml` (mingw-w64 + Vulkan-Headers + a cross-built
Vulkan-Loader import lib + glslang → `AIO-Graphics-Test.exe`). Run the **Build
AIO-Graphics-Test (Windows PE)** workflow and grab the `AIO-Graphics-Test-windows-x86_64`
artifact. The D3D11/D3D12/dxgi/d3dcompiler entry points are all loaded **dynamically**, so
the exe still launches (and shows a graceful notice) on a container without DXVK/VKD3D.

## Status

- ✅ Base cube, GPU Info (tabbed VK/GL), app shell + menu, live FPS/API HUD overlay.
- ✅ Backends: **Vulkan**, **OpenGL**, **Direct3D 11** (6-scene suite), **Direct3D 12**.
- ✅ Benchmark view — VK, GL, the five DX11 scenes, and D3D12, each with Avg/Min/Max + CSV.
- 🔜 **Direct3D 9** backend (DXVK d3d9) — the last remaining API.

## Layout

```
src/cube.c            forked vkcube + WinMain dispatch
src/menu.c            app shell (sidebar menu, GPU Info tabs, scene/benchmark pickers)
src/cube_gl.c         OpenGL backend (WGL)
src/cube_d3d11.c      Direct3D 11 scene framework + 6 scenes
src/cube_d3d12.c      Direct3D 12 backend
src/gpuinfo.c         GL + VK adapter report
src/bench.c           benchmark instrumentation (avg/min/max/1%-low + CSV)
src/hud.c             in-window FPS/API overlay
src/dolphin_assets.h  embedded DolphinVS assets (generated; see tools/)
tools/gen_dolphin_assets.py   one-time .x/.bmp/.tga → header generator
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
into `src/dolphin_assets.h`. Those assets are **© Microsoft Corporation**, included here for
the homage scene; all trademarks belong to their owners. This project is independent and not
affiliated with or endorsed by Microsoft.

Full attribution is in [`CREDITS.md`](CREDITS.md). Everything else is reimplemented natively
— no third-party `.exe` binaries are bundled.

## License

Apache-2.0 (inherited from Vulkan-Tools) — see [`LICENSE`](LICENSE).
