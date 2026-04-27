#pragma once

#ifndef BLAZECRT_OPENCL_H
#define BLAZECRT_OPENCL_H

#include "AE_Effect.h"

// Define our OpenCL parameters struct to match the arguments needed by the kernel
typedef struct OCL_CRTParams {
    float scanline_op;
    float scanline_freq;
    float scanline_soft;
    float rgb_amt;
    int rgb_mode;
    float chrom_abb;
    float grain_amt;
    int grain_size;
    float bloom_amt;
    int bloom_hq;
    float vignette_amt;
    float curvature_amt;
    unsigned int frame_count;
} OCL_CRTParams;

#ifdef __cplusplus
extern "C" {
#endif

// Initialize OpenCL context, queues, and compile program. Returns 0 on success.
int OCL_Init();

// Render 8-bit using OpenCL. Returns 0 on success.
int OCL_Render8(
    PF_Pixel8 *in_pixels,
    PF_Pixel8 *out_pixels,
    int width,
    int height,
    const OCL_CRTParams* params
);

// Render 16-bit using OpenCL. Returns 0 on success.
int OCL_Render16(
    PF_Pixel16 *in_pixels,
    PF_Pixel16 *out_pixels,
    int width,
    int height,
    const OCL_CRTParams* params
);

// Release OpenCL resources
void OCL_Shutdown();

#ifdef __cplusplus
}
#endif

#endif // BLAZECRT_OPENCL_H
