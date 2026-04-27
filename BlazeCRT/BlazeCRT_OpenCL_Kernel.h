#pragma once

const char* KERNEL_SOURCE = R"(
// --- OpenCL Kernel for BlazeCRT v2 ---

// Fast PRNG for film grain
float hash(uint x, uint y, uint frame) {
    uint seed = x * 1234567 + y * 7654321 + frame * 1000000;
    seed ^= (seed << 13);
    seed ^= (seed >> 17);
    seed ^= (seed << 5);
    return (float)seed / 4294967295.0f;
}

// 16-bit rendering kernel
__kernel void crt_render_16(
    __global const ushort4* in_pixels,
    __global ushort4* out_pixels,
    int width,
    int height,
    float scanline_op,
    float scanline_freq,
    float scanline_soft,
    float rgb_amt,
    int rgb_mode,
    float chrom_abb,
    float grain_amt,
    int grain_size,
    float bloom_amt,
    int bloom_hq,
    float vignette_amt,
    float curvature_amt,
    uint frame_count)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height) return;

    // Normalize coordinates [-1, 1]
    float nx = (x / (float)width) * 2.0f - 1.0f;
    float ny = (y / (float)height) * 2.0f - 1.0f;
    
    // Curvature (Barrel Distortion)
    float r2 = nx * nx + ny * ny;
    float distort = 1.0f + curvature_amt * r2;
    float px = nx * distort;
    float py = ny * distort;
    
    // Map back to pixel space
    float tx = (px + 1.0f) * 0.5f * width;
    float ty = (py + 1.0f) * 0.5f * height;

    // Out of bounds check for curvature
    if (tx < 0 || tx >= width || ty < 0 || ty >= height) {
        out_pixels[y * width + x] = (ushort4)(0, 0, 0, 0); // Transparent/Black
        return;
    }

    // Chromatic Aberration & RGB Split Sample points
    float2 red_offset = (float2)(0, 0);
    float2 green_offset = (float2)(0, 0);
    float2 blue_offset = (float2)(0, 0);

    // CA adds radial offset
    red_offset += (float2)(px * chrom_abb * 10.0f, py * chrom_abb * 10.0f);
    blue_offset -= (float2)(px * chrom_abb * 10.0f, py * chrom_abb * 10.0f);

    // RGB split offsets
    if (rgb_mode == 0) { // Subpixel
        int x_mod = x % 3;
        if (x_mod == 0)      { red_offset.x += 0; green_offset.x -= rgb_amt; blue_offset.x -= rgb_amt; }
        else if (x_mod == 1) { red_offset.x -= rgb_amt; green_offset.x += 0; blue_offset.x -= rgb_amt; }
        else                 { red_offset.x -= rgb_amt; green_offset.x -= rgb_amt; blue_offset.x += 0; }
    } else if (rgb_mode == 1) { // Horizontal
        red_offset.x += rgb_amt;
        blue_offset.x -= rgb_amt;
    } else if (rgb_mode == 2) { // Radial
        red_offset += (float2)(px * rgb_amt, py * rgb_amt);
        blue_offset -= (float2)(px * rgb_amt, py * rgb_amt);
    }

    // Sampling helper (nearest neighbor for now to keep it fast, or bilinear if needed)
    // We'll use nearest for simplicity and speed on custom subpixel shifts, 
    // but ideally we could use read_imagef if we passed an image2d_t. We'll stick to buffer.
    
    auto sample = [&](float sx, float sy) -> float4 {
        int ix = clamp((int)sx, 0, width - 1);
        int iy = clamp((int)sy, 0, height - 1);
        ushort4 p = in_pixels[iy * width + ix];
        return (float4)(p.x, p.y, p.z, p.w);
    };

    float r = sample(tx + red_offset.x, ty + red_offset.y).x;
    float g = sample(tx + green_offset.x, ty + green_offset.y).y;
    float b = sample(tx + blue_offset.x, ty + blue_offset.y).z;
    float a = sample(tx, ty).w; // Alpha from center

    // Bloom Approximation
    if (bloom_amt > 0.0f) {
        float3 bloom = (float3)(0,0,0);
        int taps = bloom_hq ? 4 : 1;
        float spread = bloom_hq ? 3.0f : 1.5f;
        float weight_sum = 0.0f;
        
        for(int dy = -taps; dy <= taps; dy++) {
            for(int dx = -taps; dx <= taps; dx++) {
                float w = exp(-(dx*dx + dy*dy) / (2.0f * spread * spread));
                float4 s = sample(tx + dx * 2, ty + dy * 2);
                bloom += (float3)(s.x, s.y, s.z) * w;
                weight_sum += w;
            }
        }
        bloom /= weight_sum;
        r += bloom.x * bloom_amt * 0.5f;
        g += bloom.y * bloom_amt * 0.5f;
        b += bloom.z * bloom_amt * 0.5f;
    }

    // Scanlines
    int scan_period = (int)scanline_freq;
    if (scan_period < 1) scan_period = 1;
    float scan_pos = fmod((float)ty, (float)scan_period) / scan_period;
    
    // Create a smooth valley
    float scan_mult = 1.0f;
    float valley = cos(scan_pos * 3.14159265f * 2.0f) * 0.5f + 0.5f; // 0 to 1
    // Mix using softness
    valley = mix((scan_pos < 0.5f ? 1.0f : 0.0f), valley, scanline_soft);
    
    scan_mult = mix(1.0f, valley, scanline_op);
    r *= scan_mult;
    g *= scan_mult;
    b *= scan_mult;

    // Vignette
    if (vignette_amt > 0.0f) {
        float vig = 1.0f - (r2 * vignette_amt);
        vig = clamp(vig, 0.0f, 1.0f);
        r *= vig;
        g *= vig;
        b *= vig;
    }

    // Film Grain
    if (grain_amt > 0.0f) {
        int gx = x / max(1, grain_size);
        int gy = y / max(1, grain_size);
        float noise = hash(gx, gy, frame_count) - 0.5f; // -0.5 to 0.5
        float noise_factor = 1.0f + (noise * grain_amt * 0.5f);
        r *= noise_factor;
        g *= noise_factor;
        b *= noise_factor;
    }

    // Output
    out_pixels[y * width + x] = (ushort4)(
        clamp((int)r, 0, 32768), // PF_MAX_CHAN16 is 32768
        clamp((int)g, 0, 32768),
        clamp((int)b, 0, 32768),
        clamp((int)a, 0, 32768)
    );
}

// 8-bit rendering kernel
__kernel void crt_render_8(
    __global const uchar4* in_pixels,
    __global uchar4* out_pixels,
    int width,
    int height,
    float scanline_op,
    float scanline_freq,
    float scanline_soft,
    float rgb_amt,
    int rgb_mode,
    float chrom_abb,
    float grain_amt,
    int grain_size,
    float bloom_amt,
    int bloom_hq,
    float vignette_amt,
    float curvature_amt,
    uint frame_count)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height) return;

    // Normalize coordinates [-1, 1]
    float nx = (x / (float)width) * 2.0f - 1.0f;
    float ny = (y / (float)height) * 2.0f - 1.0f;
    
    // Curvature (Barrel Distortion)
    float r2 = nx * nx + ny * ny;
    float distort = 1.0f + curvature_amt * r2;
    float px = nx * distort;
    float py = ny * distort;
    
    // Map back to pixel space
    float tx = (px + 1.0f) * 0.5f * width;
    float ty = (py + 1.0f) * 0.5f * height;

    // Out of bounds check for curvature
    if (tx < 0 || tx >= width || ty < 0 || ty >= height) {
        out_pixels[y * width + x] = (uchar4)(0, 0, 0, 0); // Transparent/Black
        return;
    }

    // Chromatic Aberration & RGB Split Sample points
    float2 red_offset = (float2)(0, 0);
    float2 green_offset = (float2)(0, 0);
    float2 blue_offset = (float2)(0, 0);

    // CA adds radial offset
    red_offset += (float2)(px * chrom_abb * 10.0f, py * chrom_abb * 10.0f);
    blue_offset -= (float2)(px * chrom_abb * 10.0f, py * chrom_abb * 10.0f);

    // RGB split offsets
    if (rgb_mode == 0) { // Subpixel
        int x_mod = x % 3;
        if (x_mod == 0)      { red_offset.x += 0; green_offset.x -= rgb_amt; blue_offset.x -= rgb_amt; }
        else if (x_mod == 1) { red_offset.x -= rgb_amt; green_offset.x += 0; blue_offset.x -= rgb_amt; }
        else                 { red_offset.x -= rgb_amt; green_offset.x -= rgb_amt; blue_offset.x += 0; }
    } else if (rgb_mode == 1) { // Horizontal
        red_offset.x += rgb_amt;
        blue_offset.x -= rgb_amt;
    } else if (rgb_mode == 2) { // Radial
        red_offset += (float2)(px * rgb_amt, py * rgb_amt);
        blue_offset -= (float2)(px * rgb_amt, py * rgb_amt);
    }

    auto sample = [&](float sx, float sy) -> float4 {
        int ix = clamp((int)sx, 0, width - 1);
        int iy = clamp((int)sy, 0, height - 1);
        uchar4 p = in_pixels[iy * width + ix];
        return (float4)(p.x, p.y, p.z, p.w);
    };

    float r = sample(tx + red_offset.x, ty + red_offset.y).x;
    float g = sample(tx + green_offset.x, ty + green_offset.y).y;
    float b = sample(tx + blue_offset.x, ty + blue_offset.y).z;
    float a = sample(tx, ty).w; // Alpha from center

    // Bloom Approximation
    if (bloom_amt > 0.0f) {
        float3 bloom = (float3)(0,0,0);
        int taps = bloom_hq ? 4 : 1;
        float spread = bloom_hq ? 3.0f : 1.5f;
        float weight_sum = 0.0f;
        
        for(int dy = -taps; dy <= taps; dy++) {
            for(int dx = -taps; dx <= taps; dx++) {
                float w = exp(-(dx*dx + dy*dy) / (2.0f * spread * spread));
                float4 s = sample(tx + dx * 2, ty + dy * 2);
                bloom += (float3)(s.x, s.y, s.z) * w;
                weight_sum += w;
            }
        }
        bloom /= weight_sum;
        r += bloom.x * bloom_amt * 0.5f;
        g += bloom.y * bloom_amt * 0.5f;
        b += bloom.z * bloom_amt * 0.5f;
    }

    // Scanlines
    int scan_period = (int)scanline_freq;
    if (scan_period < 1) scan_period = 1;
    float scan_pos = fmod((float)ty, (float)scan_period) / scan_period;
    
    // Create a smooth valley
    float scan_mult = 1.0f;
    float valley = cos(scan_pos * 3.14159265f * 2.0f) * 0.5f + 0.5f; // 0 to 1
    // Mix using softness
    valley = mix((scan_pos < 0.5f ? 1.0f : 0.0f), valley, scanline_soft);
    
    scan_mult = mix(1.0f, valley, scanline_op);
    r *= scan_mult;
    g *= scan_mult;
    b *= scan_mult;

    // Vignette
    if (vignette_amt > 0.0f) {
        float vig = 1.0f - (r2 * vignette_amt);
        vig = clamp(vig, 0.0f, 1.0f);
        r *= vig;
        g *= vig;
        b *= vig;
    }

    // Film Grain
    if (grain_amt > 0.0f) {
        int gx = x / max(1, grain_size);
        int gy = y / max(1, grain_size);
        float noise = hash(gx, gy, frame_count) - 0.5f; // -0.5 to 0.5
        float noise_factor = 1.0f + (noise * grain_amt * 0.5f);
        r *= noise_factor;
        g *= noise_factor;
        b *= noise_factor;
    }

    // Output
    out_pixels[y * width + x] = (uchar4)(
        clamp((int)r, 0, 255),
        clamp((int)g, 0, 255),
        clamp((int)b, 0, 255),
        clamp((int)a, 0, 255)
    );
}

)";
