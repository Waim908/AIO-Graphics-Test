// AIO Graphics Test - Direct3D 9 cube backend.
// Renders a spinning cube through Direct3D 9 fixed-function (tests the DXVK d3d9
// -> Vulkan -> Turnip path under Winlator). Shares the HUD overlay + benchmark.
#ifndef AIO_CUBE_D3D9_H
#define AIO_CUBE_D3D9_H

#include <windows.h>

// Opens a Direct3D 9 cube window and runs it (honoring an active --bench).
// Returns a process exit code.
int aio_run_d3d9_cube(HINSTANCE hinst);

#endif  // AIO_CUBE_D3D9_H
