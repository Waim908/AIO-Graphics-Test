// AIO Graphics Test - Direct3D 11 cube backend.
// Renders a spinning cube through Direct3D 11 (tests the DXVK -> Vulkan ->
// Turnip path under Winlator). Shares the HUD overlay and benchmark modules.
#ifndef AIO_CUBE_D3D11_H
#define AIO_CUBE_D3D11_H

#include <windows.h>

// Opens a Direct3D 11 cube window and runs it (honoring an active --bench).
// Returns a process exit code.
int aio_run_d3d11_cube(HINSTANCE hinst);

#endif  // AIO_CUBE_D3D11_H
