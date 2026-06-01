# AIO Graphics Test — Project Overview

## The deliverable
**One single Windows `.exe`** (`AIO-Graphics-Test.exe`, x86-64) that you drop into a
star/Winlator Wine container on Android. It's a graphics **diagnostic + benchmark +
multi-API renderer** in one tool — replacing the entire `3d-tests` folder of separate
demos with one binary you own and control.

## What it does — modes (pick with a flag)

| Mode | What you get | Replaces |
|---|---|---|
| *(default)* | Spinning cube — "does graphics work?" smoke test | `vkcube.exe` |
| `--gpuinfo` / `--report` | Full **OpenGL + Vulkan** dump: GPU name, **driver + API version**, memory heaps, queue families, features, extensions → printed *and* saved to a report file | `GPUInfo.exe` |
| `--bench <sec>` | **Benchmark**: runs N seconds → **avg / min / max / 1%-low FPS** + a **CSV file** (survives a session OOM-kill since it's on disk) | — |
| `--api gl\|vk\|dx9\|dx11\|dx12` | Renders the **same cube natively through each graphics API**, so one binary tests every translation path | the whole DX9/11/12 + GL demo kit |
| `--semaphore timeline\|binary` | Forces the semaphore path to **reproduce + measure** the DXVK 2.4.1-vs-2.5+ Turnip half-FPS regression | — |

## What each `--api` actually tests (the point of it)
Every API exercises a different translation layer in Winlator — that's the real
diagnostic value:

- **`vk`** → native Vulkan → **Turnip** (GPU driver, no translation)
- **`gl`** → OpenGL → **Zink / wined3d**
- **`dx9`** → **DXVK** d3d9 → Vulkan
- **`dx11`** → **DXVK** d3d11 → Vulkan
- **`dx12`** → **VKD3D-Proton** → Vulkan
- *(`dx8` optional, last)*

So a single run can tell you *which layer* is broken or slow, not just "graphics failed."

## How it's built
- **Forked from Khronos vkcube** (`cube.c`, Apache-2.0) — proven base, our additions on top.
- **CI-only**, no local builds: a GitHub Actions workflow cross-compiles Linux → Windows PE
  (mingw-w64 + Vulkan-Loader + glslang) and produces the `.exe` as a downloadable artifact.

## Build order (so it never breaks on multiple fronts)
- **v0.1** ← *current* — base cube cross-compiles green (proves the pipeline)
- **v0.2** — `--gpuinfo` (the GPUInfo.exe replacement)
- **v0.3** — `--bench` FPS/CSV benchmarking
- **v0.4** — the API backends, one at a time: `gl` → `dx11` → `dx12` → `dx9`
- **v0.5** — on-screen HUD (live FPS + driver string) + the `--semaphore` regression probe
- **v0.6** — per-API FPS in one `--bench` pass (side-by-side layer comparison)

## What lives in the repo
`src/cube.c` (the fork) + shaders + helper headers, `cmake/` cross toolchain,
`.github/workflows/build-windows.yml`, README. **No third-party `.exe`s bundled** — every
API path is our own code, so it's clean to publish.

## Net result
You retire `vkcube.exe`, `vkcubepp.exe`, `GPUInfo.exe`, and the DX9/11/12/GL demos — and
instead have **one tool** that smoke-tests, dumps GPU/driver info, benchmarks with real FPS
numbers, and renders through every API to pinpoint which translation layer is at fault.
Fully yours, maintained, and reproducibly built in CI.
