// AIO Graphics Test - benchmark instrumentation.
// Collects per-frame times during a timed run, then reports avg / min / max /
// 1%-low FPS and writes a CSV.
#ifndef AIO_BENCH_H
#define AIO_BENCH_H

// Vsync flag shared by all backends (set from the --vsync CLI flag in WinMain):
// 1 = present with vsync, 0 = uncapped. Read at swapchain/present time.
extern int aio_vsync;

// Auto-close the benchmark result popup after this many seconds (0 = wait for
// the user). Set from --autoclose <sec>; the Run All sweep passes 3.
extern int aio_autoclose_sec;

// Show the benchmark result popup. Blocks until dismissed; if aio_autoclose_sec
// > 0 it auto-dismisses after that many seconds so a sweep can proceed hands-free.
void aio_bench_show_result(const char *text);

// Start a benchmark of the given duration (seconds). Allocates the sample store.
void aio_bench_begin(int seconds);

// Override the label used for the result file + summary (e.g. semaphore-probe
// modes). Pass NULL/"" to clear. Call before the run.
void aio_bench_set_label(const char *label);

// Whether a benchmark is currently active.
int aio_bench_active(void);

// Requested benchmark duration in seconds.
int aio_bench_seconds(void);

// Record one rendered frame's time in milliseconds.
void aio_bench_add(double frame_ms);

// Finish: computes stats, writes AIO-Graphics-Test_bench.csv, and returns a
// heap-allocated human-readable summary (caller frees). Ends the benchmark.
char *aio_bench_finish(const char *api_label, double total_seconds);

#endif  // AIO_BENCH_H
