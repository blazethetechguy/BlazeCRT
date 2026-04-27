#include "BlazeCRT_OpenCL.h"
#include "BlazeCRT_OpenCL_Kernel.h"
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cl_context g_cl_context = NULL;
static cl_command_queue g_cl_queue = NULL;
static cl_program g_cl_program = NULL;
static cl_kernel g_cl_kernel_8 = NULL;
static cl_kernel g_cl_kernel_16 = NULL;
static int g_ocl_initialized = 0;

static void check_error(cl_int err, const char *operation) {
    if (err != CL_SUCCESS) {
        // Logging could go here. For now, silent fail.
    }
}

int OCL_Init() {
    if (g_ocl_initialized) return 0;

    cl_int err;
    cl_uint num_platforms;
    err = clGetPlatformIDs(0, NULL, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) return -1;

    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, NULL);

    cl_device_id selected_device = NULL;
    cl_platform_id selected_platform = NULL;

    // Prefer integrated GPU, then discrete GPU, then CPU
    for (cl_uint i = 0; i < num_platforms; i++) {
        cl_uint num_devices;
        err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);
        if (err == CL_SUCCESS && num_devices > 0) {
            cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * num_devices);
            clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);
            
            for (cl_uint j = 0; j < num_devices; j++) {
                cl_bool host_unified;
                clGetDeviceInfo(devices[j], CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl_bool), &host_unified, NULL);
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
            err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_CPU, 0, NULL, &num_devices);
            if (err == CL_SUCCESS && num_devices > 0) {
                cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * num_devices);
                clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_CPU, num_devices, devices, NULL);
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

    g_cl_context = clCreateContext(props, 1, &selected_device, NULL, NULL, &err);
    if (err != CL_SUCCESS) return -1;

    g_cl_queue = clCreateCommandQueue(g_cl_context, selected_device, 0, &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    const char* src = KERNEL_SOURCE;
    size_t src_len = strlen(src);
    g_cl_program = clCreateProgramWithSource(g_cl_context, 1, &src, &src_len, &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    err = clBuildProgram(g_cl_program, 1, &selected_device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        // Output build log if needed
        size_t log_size;
        clGetProgramBuildInfo(g_cl_program, selected_device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char* log = (char*)malloc(log_size);
        clGetProgramBuildInfo(g_cl_program, selected_device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        // printf("Build Log:\n%s\n", log);
        free(log);
        OCL_Shutdown();
        return -1;
    }

    g_cl_kernel_8 = clCreateKernel(g_cl_program, "crt_render_8", &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    g_cl_kernel_16 = clCreateKernel(g_cl_program, "crt_render_16", &err);
    if (err != CL_SUCCESS) { OCL_Shutdown(); return -1; }

    g_ocl_initialized = 1;
    return 0;
}

void OCL_Shutdown() {
    if (g_cl_kernel_8) { clReleaseKernel(g_cl_kernel_8); g_cl_kernel_8 = NULL; }
    if (g_cl_kernel_16) { clReleaseKernel(g_cl_kernel_16); g_cl_kernel_16 = NULL; }
    if (g_cl_program) { clReleaseProgram(g_cl_program); g_cl_program = NULL; }
    if (g_cl_queue) { clReleaseCommandQueue(g_cl_queue); g_cl_queue = NULL; }
    if (g_cl_context) { clReleaseContext(g_cl_context); g_cl_context = NULL; }
    g_ocl_initialized = 0;
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
    size_t buffer_size = width * height * pixel_size;

    cl_mem in_buffer = clCreateBuffer(g_cl_context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, buffer_size, in_pixels, &err);
    if (err != CL_SUCCESS) return -1;

    cl_mem out_buffer = clCreateBuffer(g_cl_context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR, buffer_size, out_pixels, &err);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(in_buffer);
        return -1;
    }

    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &in_buffer);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &out_buffer);
    err |= clSetKernelArg(kernel, 2, sizeof(int), &width);
    err |= clSetKernelArg(kernel, 3, sizeof(int), &height);
    err |= clSetKernelArg(kernel, 4, sizeof(float), &params->scanline_op);
    err |= clSetKernelArg(kernel, 5, sizeof(float), &params->scanline_freq);
    err |= clSetKernelArg(kernel, 6, sizeof(float), &params->scanline_soft);
    err |= clSetKernelArg(kernel, 7, sizeof(float), &params->rgb_amt);
    err |= clSetKernelArg(kernel, 8, sizeof(int), &params->rgb_mode);
    err |= clSetKernelArg(kernel, 9, sizeof(float), &params->chrom_abb);
    err |= clSetKernelArg(kernel, 10, sizeof(float), &params->grain_amt);
    err |= clSetKernelArg(kernel, 11, sizeof(int), &params->grain_size);
    err |= clSetKernelArg(kernel, 12, sizeof(float), &params->bloom_amt);
    err |= clSetKernelArg(kernel, 13, sizeof(int), &params->bloom_hq);
    err |= clSetKernelArg(kernel, 14, sizeof(float), &params->vignette_amt);
    err |= clSetKernelArg(kernel, 15, sizeof(float), &params->curvature_amt);
    err |= clSetKernelArg(kernel, 16, sizeof(unsigned int), &params->frame_count);

    if (err != CL_SUCCESS) {
        clReleaseMemObject(in_buffer);
        clReleaseMemObject(out_buffer);
        return -1;
    }

    size_t global_work_size[2] = { (size_t)width, (size_t)height };
    
    // We can just use NULL for local_work_size to let the driver decide
    err = clEnqueueNDRangeKernel(g_cl_queue, kernel, 2, NULL, global_work_size, NULL, 0, NULL, NULL);
    
    if (err == CL_SUCCESS) {
        // Map buffer to ensure device finishes writing to host memory (since CL_MEM_USE_HOST_PTR)
        void* mapped = clEnqueueMapBuffer(g_cl_queue, out_buffer, CL_TRUE, CL_MAP_READ, 0, buffer_size, 0, NULL, NULL, &err);
        if (mapped) {
            clEnqueueUnmapMemObject(g_cl_queue, out_buffer, mapped, 0, NULL, NULL);
        } else {
            clFinish(g_cl_queue);
        }
    } else {
        clReleaseMemObject(in_buffer);
        clReleaseMemObject(out_buffer);
        return -1;
    }

    clReleaseMemObject(in_buffer);
    clReleaseMemObject(out_buffer);
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
