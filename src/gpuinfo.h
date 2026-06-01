// AIO Graphics Test - GPU info / report mode (--gpuinfo / --report).
// Self-contained GL + Vulkan adapter dump; replaces GPUInfo.exe.
#ifndef AIO_GPUINFO_H
#define AIO_GPUINFO_H

// Dumps OpenGL (via a throwaway WGL context) and Vulkan adapter info to the
// console and to "AIO-Graphics-Test_report.txt". Returns process exit code.
int aio_run_gpuinfo(void);

#endif  // AIO_GPUINFO_H
