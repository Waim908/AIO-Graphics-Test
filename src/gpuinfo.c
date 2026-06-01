// AIO Graphics Test - GPU info / report mode (--gpuinfo / --report).
//
// Self-contained: opens its own VkInstance to enumerate Vulkan adapters, and
// spins up a throwaway WGL OpenGL context to read GL_VENDOR/RENDERER/VERSION/
// GLSL/EXTENSIONS. Output goes to the console (best-effort) AND to a report
// file, so results survive even if no console is attached. Replaces GPUInfo.exe.
//
// Copyright (c) 2026 The412Banner. Licensed under Apache-2.0 (see LICENSE).

#include <windows.h>
#include <GL/gl.h>
#include <vulkan/vulkan.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gpuinfo.h"

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif

#define AIO_REPORT_FILE "AIO-Graphics-Test_report.txt"

static FILE *g_report = NULL;

// Write to both the report file and stdout.
static void out(const char *fmt, ...) {
    va_list ap;
    if (g_report) {
        va_start(ap, fmt);
        vfprintf(g_report, fmt, ap);
        va_end(ap);
    }
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

static const char *vk_device_type_str(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                     return "Other";
    }
}

// ------------------------------------------------------------- OpenGL section
static void report_opengl(void) {
    out("\n==================== OpenGL ====================\n");

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "AIOGpuInfoGLWindow";
    if (!RegisterClassA(&wc)) {
        out("OpenGL: could not register helper window class.\n");
        return;
    }

    HWND hwnd = CreateWindowA(wc.lpszClassName, "gl", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, NULL, NULL,
                              wc.hInstance, NULL);
    if (!hwnd) {
        out("OpenGL: could not create helper window.\n");
        return;
    }

    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(dc, &pfd);
    if (pf == 0 || !SetPixelFormat(dc, pf, &pfd)) {
        out("OpenGL: no compatible pixel format (GL may be unavailable in this container).\n");
        ReleaseDC(hwnd, dc);
        DestroyWindow(hwnd);
        return;
    }

    HGLRC rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc)) {
        out("OpenGL: could not create/make-current a WGL context.\n");
        if (rc) wglDeleteContext(rc);
        ReleaseDC(hwnd, dc);
        DestroyWindow(hwnd);
        return;
    }

    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *glsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);

    out("GL_VENDOR                   : %s\n", vendor ? vendor : "(null)");
    out("GL_RENDERER                 : %s\n", renderer ? renderer : "(null)");
    out("GL_VERSION                  : %s\n", version ? version : "(null)");
    out("GL_SHADING_LANGUAGE_VERSION : %s\n", glsl ? glsl : "(null)");
    if (exts) {
        out("GL_EXTENSIONS               :\n%s\n", exts);
    } else {
        out("GL_EXTENSIONS               : (null / core profile)\n");
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
}

// ------------------------------------------------------------- Vulkan section
static void report_vulkan(void) {
    out("\n==================== Vulkan ====================\n");

    VkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "AIO Graphics Test";
    app.pEngineName = "AIO Graphics Test";
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult err = vkCreateInstance(&ici, NULL, &inst);
    if (err != VK_SUCCESS) {
        out("Vulkan: vkCreateInstance failed (err %d) - no Vulkan ICD reachable.\n", (int)err);
        return;
    }

    // Instance extensions.
    uint32_t inst_ext_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &inst_ext_count, NULL);
    if (inst_ext_count > 0) {
        VkExtensionProperties *ie = malloc(sizeof(VkExtensionProperties) * inst_ext_count);
        if (ie) {
            vkEnumerateInstanceExtensionProperties(NULL, &inst_ext_count, ie);
            out("Instance extensions (%u):\n", inst_ext_count);
            for (uint32_t i = 0; i < inst_ext_count; i++) out("  %s\n", ie[i].extensionName);
            free(ie);
        }
    }

    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(inst, &gpu_count, NULL);
    if (gpu_count == 0) {
        out("Vulkan: no physical devices found.\n");
        vkDestroyInstance(inst, NULL);
        return;
    }
    VkPhysicalDevice *gpus = malloc(sizeof(VkPhysicalDevice) * gpu_count);
    if (!gpus) {
        vkDestroyInstance(inst, NULL);
        return;
    }
    vkEnumeratePhysicalDevices(inst, &gpu_count, gpus);
    out("\nPhysical devices: %u\n", gpu_count);

    for (uint32_t g = 0; g < gpu_count; g++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(gpus[g], &p);

        uint32_t api_major = VK_API_VERSION_MAJOR(p.apiVersion);
        uint32_t api_minor = VK_API_VERSION_MINOR(p.apiVersion);
        uint32_t api_patch = VK_API_VERSION_PATCH(p.apiVersion);
        uint32_t dv_major = VK_API_VERSION_MAJOR(p.driverVersion);
        uint32_t dv_minor = VK_API_VERSION_MINOR(p.driverVersion);
        uint32_t dv_patch = VK_API_VERSION_PATCH(p.driverVersion);

        out("\n--- GPU %u ---\n", g);
        out("deviceName    : %s\n", p.deviceName);
        out("deviceType    : %s\n", vk_device_type_str(p.deviceType));
        out("apiVersion    : %u.%u.%u (0x%08x)\n", api_major, api_minor, api_patch, p.apiVersion);
        out("driverVersion : %u.%u.%u (0x%08x raw)\n", dv_major, dv_minor, dv_patch, p.driverVersion);
        out("vendorID      : 0x%04x\n", p.vendorID);
        out("deviceID      : 0x%04x\n", p.deviceID);

        // Memory heaps.
        VkPhysicalDeviceMemoryProperties mem;
        vkGetPhysicalDeviceMemoryProperties(gpus[g], &mem);
        out("memoryHeaps   : %u\n", mem.memoryHeapCount);
        for (uint32_t h = 0; h < mem.memoryHeapCount; h++) {
            double mb = (double)mem.memoryHeaps[h].size / (1024.0 * 1024.0);
            int device_local = (mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? 1 : 0;
            out("  heap %u: %.0f MB%s\n", h, mb, device_local ? " (DEVICE_LOCAL)" : "");
        }
        out("memoryTypes   : %u\n", mem.memoryTypeCount);

        // Queue families.
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[g], &qf_count, NULL);
        if (qf_count > 0) {
            VkQueueFamilyProperties *qf = malloc(sizeof(VkQueueFamilyProperties) * qf_count);
            if (qf) {
                vkGetPhysicalDeviceQueueFamilyProperties(gpus[g], &qf_count, qf);
                out("queueFamilies : %u\n", qf_count);
                for (uint32_t q = 0; q < qf_count; q++) {
                    VkQueueFlags f = qf[q].queueFlags;
                    out("  family %u: count=%u%s%s%s%s\n", q, qf[q].queueCount,
                        (f & VK_QUEUE_GRAPHICS_BIT) ? " GRAPHICS" : "",
                        (f & VK_QUEUE_COMPUTE_BIT) ? " COMPUTE" : "",
                        (f & VK_QUEUE_TRANSFER_BIT) ? " TRANSFER" : "",
                        (f & VK_QUEUE_SPARSE_BINDING_BIT) ? " SPARSE" : "");
                }
                free(qf);
            }
        }

        // Device extensions.
        uint32_t dev_ext_count = 0;
        vkEnumerateDeviceExtensionProperties(gpus[g], NULL, &dev_ext_count, NULL);
        if (dev_ext_count > 0) {
            VkExtensionProperties *de = malloc(sizeof(VkExtensionProperties) * dev_ext_count);
            if (de) {
                vkEnumerateDeviceExtensionProperties(gpus[g], NULL, &dev_ext_count, de);
                out("deviceExtensions (%u):\n", dev_ext_count);
                for (uint32_t i = 0; i < dev_ext_count; i++) out("  %s\n", de[i].extensionName);
                free(de);
            }
        }
    }

    free(gpus);
    vkDestroyInstance(inst, NULL);
}

// ----------------------------------------------------------------- entry point
int aio_run_gpuinfo(void) {
    // Try to attach to a parent console so console output is visible when launched
    // from a Wine/CMD console; otherwise the report file is the primary channel.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
    }

    g_report = fopen(AIO_REPORT_FILE, "w");

    out("AIO Graphics Test - GPU info report\n");
    out("(OpenGL + Vulkan adapter dump; replaces GPUInfo.exe)\n");

    report_opengl();
    report_vulkan();

    out("\nReport written to %s\n", AIO_REPORT_FILE);

    if (g_report) {
        fclose(g_report);
        g_report = NULL;
    }
    return 0;
}
