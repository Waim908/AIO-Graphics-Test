// AIO Graphics Test - Direct3D 10 cube backend.
// Renders a spinning cube through Direct3D 10 (tests the DXVK d3d10 -> d3d11 ->
// Vulkan -> Turnip path under Winlator). Shares the HUD overlay + benchmark.
#ifndef AIO_CUBE_D3D10_H
#define AIO_CUBE_D3D10_H

#include <windows.h>

// Opens a Direct3D 10 cube window and runs it (honoring an active --bench).
// Returns a process exit code.
int aio_run_d3d10_cube(HINSTANCE hinst);

#endif  // AIO_CUBE_D3D10_H
