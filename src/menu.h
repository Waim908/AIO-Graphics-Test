// AIO Graphics Test - in-app start menu.
// On launch the .exe shows a menu (app-name header + buttons) to pick a test.
#ifndef AIO_MENU_H
#define AIO_MENU_H

#include <windows.h>

enum AioMode {
    AIO_MODE_EXIT = 0,
    AIO_MODE_CUBE_VK,
    AIO_MODE_CUBE_GL,
    AIO_MODE_CUBE_DX9,
    AIO_MODE_CUBE_DX11,
    AIO_MODE_CUBE_DX12,
    AIO_MODE_GPUINFO,
    AIO_MODE_BENCH
};

// Shows the two-level start menu and blocks until the user picks an option.
// Returns the selected AioMode (AIO_MODE_EXIT if the window is closed).
int aio_show_menu(HINSTANCE hInstance);

#endif  // AIO_MENU_H
