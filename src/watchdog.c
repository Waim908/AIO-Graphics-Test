// AIO Graphics Test - render-loop watchdog (see watchdog.h).
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "watchdog.h"

static volatile LONG g_run;
static volatile uint64_t *g_counter;
static int g_timeout;
static HANDLE g_thread;

static DWORD WINAPI wd_proc(LPVOID unused) {
    (void)unused;
    uint64_t last = g_counter ? *g_counter : 0;
    int stuck = 0;
    while (g_run) {
        Sleep(1000);
        uint64_t cur = g_counter ? *g_counter : last;
        if (cur == last) {
            if (++stuck >= g_timeout) {
                // The render loop is wedged (e.g. SwapBuffers deadlock). Bail so
                // the stuck window closes instead of hanging the container.
                ExitProcess(3);
            }
        } else {
            stuck = 0;
            last = cur;
        }
    }
    return 0;
}

void aio_watchdog_start(volatile uint64_t *counter, int timeout_sec) {
    if (g_thread) return;
    g_counter = counter;
    g_timeout = (timeout_sec > 0) ? timeout_sec : 12;
    g_run = 1;
    g_thread = CreateThread(NULL, 0, wd_proc, NULL, 0, NULL);
}

void aio_watchdog_stop(void) {
    g_run = 0;
    if (g_thread) {
        CloseHandle(g_thread);  // detach; it exits on the next g_run check
        g_thread = NULL;
    }
}
