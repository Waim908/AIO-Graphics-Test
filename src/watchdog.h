// AIO Graphics Test - render-loop watchdog.
//
// A diagnostic that runs potentially-broken graphics stacks must not hang the
// whole container when a backend deadlocks (e.g. OpenGL via a misconfigured
// wined3d/Zink blocking in SwapBuffers). The watchdog samples a frame counter on
// a background thread; if it makes no progress for `timeout_sec`, it force-exits
// the process so the stuck window closes and the container is freed.
#ifndef AIO_WATCHDOG_H
#define AIO_WATCHDOG_H

#include <stdint.h>

// Start watching *counter (the loop's frame count). If it doesn't change for
// timeout_sec seconds, ExitProcess() is called. Safe to call once per run.
void aio_watchdog_start(volatile uint64_t *counter, int timeout_sec);

// Stop the watchdog (call on normal loop exit).
void aio_watchdog_stop(void);

#endif  // AIO_WATCHDOG_H
