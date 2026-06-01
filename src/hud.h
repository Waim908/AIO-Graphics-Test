// AIO Graphics Test - in-window FPS/API HUD overlay.
// A small always-on-top, click-through bar painted over a render window's
// top-left corner. Shared by every cube backend (Vulkan, OpenGL, ...).
#ifndef AIO_HUD_H
#define AIO_HUD_H

#include <windows.h>

void aio_hud_create(HINSTANCE hinst);
void aio_hud_update(HWND render_window, const char *text);
void aio_hud_destroy(void);

#endif  // AIO_HUD_H
