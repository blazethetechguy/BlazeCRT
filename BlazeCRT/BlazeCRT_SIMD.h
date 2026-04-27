#pragma once
#ifndef BLAZECRT_SIMD_H
#define BLAZECRT_SIMD_H

/*  BlazeCRT v3 – SSE / AVX SIMD Helpers
 *  ──────────────────────────────────────
 *  Processes a pixel's {R,G,B,A} as a single __m128 vector.
 *  Every multiply / clamp becomes ONE instruction instead of four.
 */

#include <immintrin.h>   /* SSE2-4.2 + AVX/AVX2 intrinsics */

/* ── Load a PF_Pixel8 into a float4 {R, G, B, A} ─────────────── */
static __forceinline __m128 simd_load_pixel8(float fR, float fG, float fB, float fA) {
	return _mm_setr_ps(fR, fG, fB, fA);
}

/* ── Broadcast a scalar to all 4 lanes ─────────────────────────── */
static __forceinline __m128 simd_splat(float v) {
	return _mm_set1_ps(v);
}

/* ── Multiply RGBA by a uniform scalar (scanline, vignette, etc.) */
static __forceinline __m128 simd_mul_scalar(__m128 rgba, float s) {
	return _mm_mul_ps(rgba, _mm_set1_ps(s));
}

/* ── Multiply RGBA by per-channel factors {rMul, gMul, bMul, aMul} */
static __forceinline __m128 simd_mul_channels(__m128 rgba, float rM, float gM, float bM, float aM) {
	return _mm_mul_ps(rgba, _mm_setr_ps(rM, gM, bM, aM));
}

/* ── Additive blend (bloom, etc.) ──────────────────────────────── */
static __forceinline __m128 simd_add(__m128 a, __m128 b) {
	return _mm_add_ps(a, b);
}

/* ── Clamp to [0, maxVal] – branchless ─────────────────────────── */
static __forceinline __m128 simd_clamp(__m128 v, float maxVal) {
	v = _mm_max_ps(v, _mm_setzero_ps());
	v = _mm_min_ps(v, _mm_set1_ps(maxVal));
	return v;
}

/* ── Extract lanes from __m128 {R, G, B, A} ────────────────────── */
static __forceinline float simd_extract_r(__m128 v) {
	float r; _mm_store_ss(&r, v); return r;
}
static __forceinline float simd_extract_g(__m128 v) {
	float out[4]; _mm_storeu_ps(out, v); return out[1];
}
static __forceinline float simd_extract_b(__m128 v) {
	float out[4]; _mm_storeu_ps(out, v); return out[2];
}
static __forceinline float simd_extract_a(__m128 v) {
	float out[4]; _mm_storeu_ps(out, v); return out[3];
}

/* ── Store __m128 to 4-float array ─────────────────────────────── */
static __forceinline void simd_store(float* dst, __m128 v) {
	_mm_storeu_ps(dst, v);
}

/* ── Branchless select: mask ? a : b ───────────────────────────── */
static __forceinline __m128 simd_select(__m128 mask, __m128 a, __m128 b) {
	return _mm_blendv_ps(b, a, mask);
}

/* ── Fast integer hash (unchanged, scalar – already branchless) ── */
static __forceinline unsigned int simd_hash(unsigned int x, unsigned int y, unsigned int frame) {
	unsigned int seed = x * 1234567u + y * 7654321u + frame * 1013904223u;
	seed ^= (seed << 13);
	seed ^= (seed >> 17);
	seed ^= (seed << 5);
	return seed;
}

/* ── Pre-calculated sin LUT (256 entries, covers 0..2π) ────────── */
#define BLAZE_SIN_LUT_SIZE 256
static float g_sinLUT[BLAZE_SIN_LUT_SIZE];
static int   g_sinLUT_init = 0;

static __forceinline void simd_init_sin_lut(void) {
	if (g_sinLUT_init) return;
	for (int i = 0; i < BLAZE_SIN_LUT_SIZE; i++) {
		g_sinLUT[i] = sinf((float)i / (float)BLAZE_SIN_LUT_SIZE * 6.28318530718f);
	}
	g_sinLUT_init = 1;
}

/* ── Fast sin approximation via LUT (no trig in hot path) ──────── */
static __forceinline float fast_sin(float x) {
	/* Normalize x to [0, 2π) */
	float norm = x * (1.0f / 6.28318530718f);
	norm = norm - floorf(norm);  /* frac part, now in [0,1) */
	int idx = (int)(norm * (float)BLAZE_SIN_LUT_SIZE) & (BLAZE_SIN_LUT_SIZE - 1);
	return g_sinLUT[idx];
}

static __forceinline float fast_cos(float x) {
	return fast_sin(x + 1.57079632679f); /* cos(x) = sin(x + π/2) */
}

#endif /* BLAZECRT_SIMD_H */
