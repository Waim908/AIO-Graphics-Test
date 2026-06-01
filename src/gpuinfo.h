// AIO Graphics Test - GPU info / report mode (--gpuinfo / --report).
// Self-contained GL + Vulkan adapter dump; replaces GPUInfo.exe.
#ifndef AIO_GPUINFO_H
#define AIO_GPUINFO_H

// Per-API reports for the GPU Info tabs (caller frees). CRLF-terminated.
char *aio_gpuinfo_build_gl_text(void);
char *aio_gpuinfo_build_vk_text(void);

// Builds the full GL + Vulkan adapter report as a heap string (caller frees).
// Lines are CRLF-terminated so it displays correctly in a Win32 EDIT control.
char *aio_gpuinfo_build_text(void);

// CLI path: builds the report and writes it to the console and to
// "AIO-Graphics-Test_report.txt". Returns a process exit code.
int aio_run_gpuinfo(void);

#endif  // AIO_GPUINFO_H
