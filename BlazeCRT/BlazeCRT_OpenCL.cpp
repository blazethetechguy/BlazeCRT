#include "BlazeCRT_OpenCL.h"
#include "BlazeCRT_OpenCL_Kernel.h"

#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AE_OS_WIN
#include <Windows.h>
#endif

// --- Dynamic OpenCL function pointers ---
typedef cl_int (CL_API_CALL *pfn_clGetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
typedef cl_int (CL_API_CALL *pfn_clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
typedef cl_int (CL_API_CALL *pfn_clGetDeviceInfo)(cl_device_id, cl_device_info, size_t, void*, size_t*);
typedef cl_context (CL_API_CALL *pfn_clCreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*, void (CL_CALLBACK*)(const char*, const void*, size_t, void*), void*, cl_int*);
typedef cl_command_queue (CL_API_CALL *pfn_clCreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int*);
typedef cl_program (CL_API_CALL *pfn_clCreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
typedef cl_int (CL_API_CALL *pfn_clBuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void (CL_CALLBACK*)(cl_program, void*), void*);
typedef cl_int (CL_API_CALL *pfn_clGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void*, size_t*);
typedef cl_kernel (CL_API_CALL *pfn_clCreateKernel)(cl_program, const char*, cl_int*);
typedef cl_int (CL_API_CALL *pfn_clSetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
typedef cl_mem (CL_API_CALL *pfn_clCreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*);
typedef cl_int (CL_API_CALL *pfn_clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, const size_t*, cl_uint, const cl_event*, cl_event*);
typedef void* (CL_API_CALL *pfn_clEnqueueMapBuffer)(cl_command_queue, cl_mem, cl_bool, cl_map_flags, size_t, size_t, cl_uint, const cl_event*, cl_event*, cl_int*);
typedef cl_int (CL_API_CALL *pfn_clEnqueueUnmapMemObject)(cl_command_queue, cl_mem, void*, cl_uint, const cl_event*, cl_event*);
typedef cl_int (CL_API_CALL *pfn_clFinish)(cl_command_queue);
typedef cl_int (CL_API_CALL *pfn_clReleaseMemObject)(cl_mem);
typedef cl_int (CL_API_CALL *pfn_clReleaseKernel)(cl_kernel);
typedef cl_int (CL_API_CALL *pfn_clReleaseProgram)(cl_program);
typedef cl_int (CL_API_CALL *pfn_clReleaseCommandQueue)(cl_command_queue);
typedef cl_int (CL_API_CALL *pfn_clReleaseContext)(cl_context);

// --- Global function pointer instances ---
static pfn_clGetPlatformIDs         p_clGetPlatformIDs = NULL;
static pfn_clGetDeviceIDs           p_clGetDeviceIDs = NULL;
static pfn_clGetDeviceInfo          p_clGetDeviceInfo = NULL;
static pfn_clCreateContext          p_clCreateContext = NULL;
static pfn_clCreateCommandQueue     p_clCreateCommandQueue = NULL;
static pfn_clCreateProgramWithSource p_clCreateProgramWithSource = NULL;
static pfn_clBuildProgram           p_clBuildProgram = NULL;
static pfn_clGetProgramBuildInfo    p_clGetProgramBuildInfo = NULL;
static pfn_clCreateKernel           p_clCreateKernel = NULL;
static pfn_clSetKernelArg           p_clSetKernelArg = NULL;
static pfn_clCreateBuffer           p_clCreateBuffer = NULL;
static pfn_clEnqueueNDRangeKernel   p_clEnqueueNDRangeKernel = NULL;
static pfn_clEnqueueMapBuffer       p_clEnqueueMapBuffer = NULL;
static pfn_clEnqueueUnmapMemObject  p_clEnqueueUnmapMemObject = NULL;
static pfn_clFinish                 p_clFinish = NULL;
static pfn_clReleaseMemObject       p_clReleaseMemObject = NULL;
static pfn_clReleaseKernel          p_clReleaseKernel = NULL;
static pfn_clReleaseProgram         p_clReleaseProgram = NULL;
static pfn_clReleaseCommandQueue    p_clReleaseCommandQueue = NULL;
static pfn_clReleaseContext         p_clReleaseContext = NULL;

static HMODULE g_ocl_dll = NULL;
static cl_context g_cl_context = NULL;
static cl_command_queue g_cl_queue = NULL;
static cl_program g_cl_program = NULL;
static cl_kernel g_cl_kernel_8 = NULL;
static cl_kernel g_cl_kernel_16 = NULL;
static int g_ocl_initialized = 0;
static int g_ocl_load_failed = 0;

static int OCL_LoadLibrary() {
    if (g_ocl_dll) return 0;
    if (g_ocl_load_failed) return -1;

    g_ocl_dll = LoadLibraryA("OpenCL.dll");
    if (!g_ocl_dll) {
        g_ocl_load_failed = 1;
        return -1;
    }

#define LOAD_CL_FUNC(name) \
    p_##name = (pfn_##name)GetProcAddress(g_ocl_dll, #name); \
    if (!p_##name) { FreeLibrary(g_ocl_dll); g_ocl_dll = NULL; g_ocl_load_failed = 1; return -1; }

    LOAD_CL_FUNC(clGetPlatformIDs);
    LOAD_CL_FUNC(clGetDeviceIDs);
    LOAD_CL_FUNC(clGetDeviceInfo);
    LOAD_CL_FUNC(clCreateContext);
    LOAD_CL_FUNC(clCreateCommandQueue);
    LOAD_CL_FUNC(clCreateProgramWithSource);
    LOAD_CL_FUNC(clBuildProgram);
    LOAD_CL_FUNC(clGetProgramBuildInfo);
    LOAD_CL_FUNC(clCreateKernel);
    LOAD_CL_FUNC(clSetKernelArg);
    LOAD_CL_FUNC(clCreateBuffer);
    LOAD_CL_FUNC(clEnqueueNDRangeKernel);
    LOAD_CL_FUNC(clEnqueueMapBuffer);
    LOAD_CL_FUNC(clEnqueueUnmapMemObject);
    LOAD_CL_FUNC(clFinish);
    LOAD_CL_FUNC(clReleaseMemObject);
    LOAD_CL_FUNC(clReleaseKernel);
    LOAD_CL_FUNC(clReleaseProgram);
    LOAD_CL_FUNC(clReleaseCommandQueue);
    LOAD_CL_FUNC(clReleaseContext);

#undef LOAD_CL_FUNC

    return 0;
}

int OCL_Init() {
    if (g_ocl_initialized) return 0;
    if (OCL_LoadLibrary() != 0) return -1;

    cl_int err;
    cl_uint num_platforms;
    err = p_clGetPlatformIDs(0, NULL, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) return -1;

    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * num_platforms);
    p_clGetPlatformIDs(num_platforms, platforms, NULL);

    cl_device_id selected_device = NULL;
    cl_platform_id selected_platform = NULL;

    // Prefer integrated GPU (shared memory), then discrete GPU, then CPU
    for (cl_uint i = 0; i < num_platforms; i++) {
        cl_uint num_devices;
        err = p_clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);
        if (err == CL_SUCCESS && num_devices > 0) {
            cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * num_devices);
            p_clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);
            
            for (cl_uint j = 0; j < num_devices; j++) {
                cl_bool host_unified;
                p_clGetDeviceInfo(devices[j], CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl_bool), &host_unified, NULL);
                if (host_unified) {
                    selected_device = devices[j];
                    selected_platform = platforms[i];
                    break;
                }
            }
            if (selected_device == NULL) {
                selected_device = devices[0];
                selected_platform = platforms[i];
            }
            free(devices);
            if (selected_device != NULL) break;
        }
    }

    if (selected_device == NULL) {
        for (cl_uint i = 0; i < num_platforms; i++) {
            cl_uint num_devices;
            err = p_clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_CPU, 0, NULL, &num_devices);
            if (err == CL_SUCCESS && num_devices > 0) {
                cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * num_devices);
                p_clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_CPU, num_devices, devices, NULL);
                selected_device = devices[0];
                selected_platform = platforms[i];
                free(devices);
                break;
            }
        }
    }

    free(platforms);

    if (selected_device == NULL) return -1;

    cl_context_properties props[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)selected_platform,
        0
    };

    g_cl_context = p_clCreateContext(props, 1, &selected_device, NULL, NULL, &err);
    if (err != CL_SUCCESS) return -1;

    g_cl_queue = p_clCreateCommandQueue(g_cl_context, selected_device, 0, &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    const char* src = KERNEL_SOURCE;
    size_t src_len = strlen(src);
    g_cl_program = p_clCreateProgramWithSource(g_cl_context, 1, &src, &src_len, &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    err = p_clBuildProgram(g_cl_program, 1, &selected_device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size;
        p_clGetProgramBuildInfo(g_cl_program, selected_device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char* build_log = (char*)malloc(log_size);
        p_clGetProgramBuildInfo(g_cl_program, selected_device, CL_PROGRAM_BUILD_LOG, log_size, build_log, NULL);
        free(build_log);
        OCL_Shutdown();
        return -1;
    }

    g_cl_kernel_8 = p_clCreateKernel(g_cl_program, "crt_render_8", &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    g_cl_kernel_16 = p_clCreateKernel(g_cl_program, "crt_render_16", &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    g_ocl_initialized = 1;
    return 0;
}

void OCL_Shutdown() {
    if (g_cl_kernel_8) { p_clReleaseKernel(g_cl_kernel_8); g_cl_kernel_8 = NULL; }
    if (g_cl_kernel_16) { p_clReleaseKernel(g_cl_kernel_16); g_cl_kernel_16 = NULL; }
    if (g_cl_program) { p_clReleaseProgram(g_cl_program); g_cl_program = NULL; }
    if (g_cl_queue) { p_clReleaseCommandQueue(g_cl_queue); g_cl_queue = NULL; }
    if (g_cl_context) { p_clReleaseContext(g_cl_context); g_cl_context = NULL; }
    g_ocl_initialized = 0;

    if (g_ocl_dll) {
        FreeLibrary(g_ocl_dll);
        g_ocl_dll = NULL;
    }
}

static int OCL_Render_Impl(
    void *in_pixels,
    void *out_pixels,
    int width,
    int height,
    const OCL_CRTParams* params,
    cl_kernel kernel,
    size_t pixel_size
) {
    if (!g_ocl_initialized) {
        if (OCL_Init() != 0) return -1;
    }

    cl_int err;
    size_t buffer_size = (size_t)width * (size_t)height * pixel_size;

    cl_mem in_buffer = p_clCreateBuffer(g_cl_context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, buffer_size, in_pixels, &err);
    if (err != CL_SUCCESS) return -1;

    cl_mem out_buffer = p_clCreateBuffer(g_cl_context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR, buffer_size, out_pixels, &err);
    if (err != CL_SUCCESS) {
        p_clReleaseMemObject(in_buffer);
        return -1;
    }

    err = p_clSetKernelArg(kernel, 0, sizeof(cl_mem), &in_buffer);
    err |= p_clSetKernelArg(kernel, 1, sizeof(cl_mem), &out_buffer);
    err |= p_clSetKernelArg(kernel, 2, sizeof(int), &width);
    err |= p_clSetKernelArg(kernel, 3, sizeof(int), &height);
    err |= p_clSetKernelArg(kernel, 4, sizeof(float), &params->scanline_op);
    err |= p_clSetKernelArg(kernel, 5, sizeof(float), &params->scanline_freq);
    err |= p_clSetKernelArg(kernel, 6, sizeof(float), &params->scanline_soft);
    err |= p_clSetKernelArg(kernel, 7, sizeof(float), &params->rgb_amt);
    err |= p_clSetKernelArg(kernel, 8, sizeof(int), &params->rgb_mode);
    err |= p_clSetKernelArg(kernel, 9, sizeof(float), &params->chrom_abb);
    err |= p_clSetKernelArg(kernel, 10, sizeof(float), &params->grain_amt);
    err |= p_clSetKernelArg(kernel, 11, sizeof(int), &params->grain_size);
    err |= p_clSetKernelArg(kernel, 12, sizeof(float), &params->bloom_amt);
    err |= p_clSetKernelArg(kernel, 13, sizeof(int), &params->bloom_hq);
    err |= p_clSetKernelArg(kernel, 14, sizeof(float), &params->vignette_amt);
    err |= p_clSetKernelArg(kernel, 15, sizeof(float), &params->curvature_amt);
    err |= p_clSetKernelArg(kernel, 16, sizeof(unsigned int), &params->frame_count);

    if (err != CL_SUCCESS) {
        p_clReleaseMemObject(in_buffer);
        p_clReleaseMemObject(out_buffer);
        return -1;
    }

    size_t global_work_size[2] = { (size_t)width, (size_t)height };
    
    err = p_clEnqueueNDRangeKernel(g_cl_queue, kernel, 2, NULL, global_work_size, NULL, 0, NULL, NULL);
    
    if (err == CL_SUCCESS) {
        void* mapped = p_clEnqueueMapBuffer(g_cl_queue, out_buffer, CL_TRUE, CL_MAP_READ, 0, buffer_size, 0, NULL, NULL, &err);
        if (mapped) {
            p_clEnqueueUnmapMemObject(g_cl_queue, out_buffer, mapped, 0, NULL, NULL);
        } else {
            p_clFinish(g_cl_queue);
        }
    } else {
        p_clReleaseMemObject(in_buffer);
        p_clReleaseMemObject(out_buffer);
        return -1;
    }

    p_clReleaseMemObject(in_buffer);
    p_clReleaseMemObject(out_buffer);
    return 0;
}

int OCL_Render8(
    PF_Pixel8 *in_pixels,
    PF_Pixel8 *out_pixels,
    int width,
    int height,
    const OCL_CRTParams* params
) {
    return OCL_Render_Impl((void*)in_pixels, (void*)out_pixels, width, height, params, g_cl_kernel_8, sizeof(PF_Pixel8));
}

int OCL_Render16(
    PF_Pixel16 *in_pixels,
    PF_Pixel16 *out_pixels,
    int width,
    int height,
    const OCL_CRTParams* params
) {
    return OCL_Render_Impl((void*)in_pixels, (void*)out_pixels, width, height, params, g_cl_kernel_16, sizeof(PF_Pixel16));
}
