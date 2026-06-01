// AIO Graphics Test - OpenGL cube backend.
// Renders a spinning cube via a WGL OpenGL context (tests the wined3d / Zink ->
// Vulkan path under Winlator). Shares the HUD overlay and benchmark modules.
#ifndef AIO_CUBE_GL_H
#define AIO_CUBE_GL_H

#include <windows.h>

// Opens an OpenGL cube window and runs it (honoring an active --bench). Returns
// a process exit code.
int aio_run_gl_cube(HINSTANCE hinst);

#endif  // AIO_CUBE_GL_H
