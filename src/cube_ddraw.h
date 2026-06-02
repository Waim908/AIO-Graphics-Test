// AIO Graphics Test - DirectDraw / legacy Direct3D backend (DX5 / DX6 / DX7).
//
// Renders through DirectDraw + the Direct3D immediate-mode device. Under Winlator
// this exercises a path *nothing else in the tool touches*: DXVK does NOT
// implement ddraw, so this goes ddraw.dll -> Wine's built-in ddraw -> wined3d ->
// OpenGL -> Zink -> Vulkan (the legacy stack old DX5/6/7 games actually use).
//
// Variants (the "scene" string):
//   "dd7"  - Direct3D 7  immediate-mode cube (IDirect3DDevice7)   [the clean one]
//   "dd6"  - Direct3D 6  immediate-mode cube (IDirect3DDevice3 + legacy viewport)
//   "dd5"  - Direct3D 5  immediate-mode cube (IDirect3DDevice2 + legacy viewport)
//   "dd2d" - pure 2D DirectDraw blit test (no Direct3D at all)
#ifndef AIO_CUBE_DDRAW_H
#define AIO_CUBE_DDRAW_H

#include <windows.h>

// Opens a DirectDraw window and runs the selected variant (honoring --bench).
// variant: "dd7" (default), "dd6", "dd5", or "dd2d". Returns a process exit code.
int aio_run_ddraw_cube(HINSTANCE hinst, const char *variant);

#endif  // AIO_CUBE_DDRAW_H
