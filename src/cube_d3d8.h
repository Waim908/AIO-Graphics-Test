// AIO Graphics Test - Direct3D 8 cube backend.
// Renders a spinning cube through Direct3D 8 fixed-function (tests the DXVK d3d8
// -> d3d9 -> Vulkan -> Turnip path under Winlator). Shares HUD + benchmark.
#ifndef AIO_CUBE_D3D8_H
#define AIO_CUBE_D3D8_H

#include <windows.h>

// Opens a Direct3D 8 cube window and runs it (honoring an active --bench).
// Returns a process exit code.
int aio_run_d3d8_cube(HINSTANCE hinst);

#endif  // AIO_CUBE_D3D8_H
