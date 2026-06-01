// AIO Graphics Test - benchmark instrumentation.
//
// Stores per-frame times for a timed run, then computes avg / min / max / 1%-low
// FPS and writes a CSV. "1% low" = the average FPS over the slowest 1% of frames
// (the standard frametime-percentile definition).
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"

#define AIO_BENCH_CSV "AIO-Graphics-Test_bench.csv"

static double *g_ft;   // per-frame times (ms)
static size_t g_n;     // sample count
static size_t g_cap;   // capacity
static int g_seconds;  // requested duration
static int g_active;
static int g_warmup;   // first frames to discard (pipeline/swapchain warm-up)
static char g_label_override[64];  // if set, overrides the result label (probe modes)
int aio_vsync = 0;                 // shared vsync flag (set from --vsync)

void aio_bench_set_label(const char *label) {
    if (label && label[0])
        snprintf(g_label_override, sizeof(g_label_override), "%s", label);
    else
        g_label_override[0] = '\0';
}

// Frames faster than this (ms) are measurement artifacts, not real rendered
// frames (a windowed Present alone costs more than this), so they're dropped to
// keep Max FPS meaningful. 0.02 ms == 50000 FPS, well above any real result.
#define AIO_BENCH_MIN_FT_MS 0.02
#define AIO_BENCH_WARMUP_FRAMES 3

void aio_bench_begin(int seconds) {
    g_seconds = seconds;
    g_active = 1;
    g_n = 0;
    g_cap = 4096;
    g_warmup = AIO_BENCH_WARMUP_FRAMES;
    g_ft = (double *)malloc(g_cap * sizeof(double));
}

int aio_bench_active(void) { return g_active; }
int aio_bench_seconds(void) { return g_seconds; }

void aio_bench_add(double frame_ms) {
    if (!g_active || !g_ft || frame_ms <= 0.0) return;
    if (g_warmup > 0) {  // discard warm-up frames (huge or near-zero first frames)
        g_warmup--;
        return;
    }
    if (frame_ms < AIO_BENCH_MIN_FT_MS) return;  // drop impossible sub-frame outliers
    if (g_n >= g_cap) {
        size_t nc = g_cap * 2;
        double *p = (double *)realloc(g_ft, nc * sizeof(double));
        if (!p) return;
        g_ft = p;
        g_cap = nc;
    }
    g_ft[g_n++] = frame_ms;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

char *aio_bench_finish(const char *api_label, double total_seconds) {
    if (!api_label) api_label = "";
    if (g_label_override[0]) api_label = g_label_override;  // probe modes override the label

    if (!g_ft || g_n == 0) {
        g_active = 0;
        if (g_ft) { free(g_ft); g_ft = NULL; }
        char *s = (char *)malloc(64);
        if (s) strcpy(s, "No frames were captured.");
        return s;
    }

    double minft = g_ft[0], maxft = g_ft[0], sum = 0.0;
    for (size_t i = 0; i < g_n; i++) {
        double v = g_ft[i];
        sum += v;
        if (v < minft) minft = v;
        if (v > maxft) maxft = v;
    }

    double avg_fps = (total_seconds > 0.0) ? (double)g_n / total_seconds : (1000.0 * (double)g_n / sum);
    double max_fps = (minft > 0.0) ? 1000.0 / minft : 0.0;
    double min_fps = (maxft > 0.0) ? 1000.0 / maxft : 0.0;

    // 1% low: average FPS over the slowest 1% of frames.
    double *sorted = (double *)malloc(g_n * sizeof(double));
    double low1_fps = 0.0;
    if (sorted) {
        memcpy(sorted, g_ft, g_n * sizeof(double));
        qsort(sorted, g_n, sizeof(double), cmp_double);  // ascending
        size_t k = g_n / 100;
        if (k < 1) k = 1;
        double s2 = 0.0;
        for (size_t i = 0; i < k; i++) s2 += sorted[g_n - 1 - i];  // slowest k frametimes
        low1_fps = (s2 > 0.0) ? 1000.0 * (double)k / s2 : 0.0;
        free(sorted);
    }

    FILE *f = fopen(AIO_BENCH_CSV, "w");
    if (f) {
        fprintf(f, "# AIO Graphics Test benchmark - %s\n", api_label);
        fprintf(f, "# frames,%lu,seconds,%.3f\n", (unsigned long)g_n, total_seconds);
        fprintf(f, "# avg_fps,%.2f,min_fps,%.2f,max_fps,%.2f,low1pct_fps,%.2f\n", avg_fps, min_fps,
                max_fps, low1_fps);
        fprintf(f, "frame,frame_ms,fps\n");
        for (size_t i = 0; i < g_n; i++) {
            double v = g_ft[i];
            fprintf(f, "%lu,%.4f,%.2f\n", (unsigned long)i, v, (v > 0.0) ? 1000.0 / v : 0.0);
        }
        fclose(f);
    }

    // Compact result file (read by the shell's Benchmark view): avg|min|max FPS.
    // The view shows only these three; full stats stay in the CSV + popup.
    {
        char rfn[160];
        snprintf(rfn, sizeof(rfn), "AIO-Graphics-Test_bench_%s.txt", api_label);
        FILE *rf = fopen(rfn, "w");
        if (rf) {
            fprintf(rf, "%.0f|%.0f|%.0f", avg_fps, min_fps, max_fps);
            fclose(rf);
        }
    }

    char *summary = (char *)malloc(512);
    if (summary) {
        snprintf(summary, 512,
                 "%s benchmark\n\n"
                 "Duration : %.1f s\n"
                 "Frames   : %lu\n\n"
                 "Avg FPS  : %.1f\n"
                 "Min FPS  : %.1f\n"
                 "Max FPS  : %.1f\n"
                 "1%% low   : %.1f\n\n"
                 "Saved: " AIO_BENCH_CSV,
                 api_label, total_seconds, (unsigned long)g_n, avg_fps, min_fps, max_fps, low1_fps);
    }

    free(g_ft);
    g_ft = NULL;
    g_n = 0;
    g_active = 0;
    return summary;
}
