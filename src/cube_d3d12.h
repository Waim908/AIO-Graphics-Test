// AIO Graphics Test - Direct3D 12 cube backend.
// Renders a spinning cube through Direct3D 12 (tests the VKD3D-Proton ->
// Vulkan -> Turnip path under Winlator). Shares the HUD overlay + benchmark.
#ifndef AIO_CUBE_D3D12_H
#define AIO_CUBE_D3D12_H

#include <windows.h>

// Opens a Direct3D 12 cube window and runs it (honoring an active --bench).
// Returns a process exit code.
int aio_run_d3d12_cube(HINSTANCE hinst);

#endif  // AIO_CUBE_D3D12_H
