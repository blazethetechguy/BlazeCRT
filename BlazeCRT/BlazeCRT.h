#pragma once
#ifndef BLAZECRT_H
#define BLAZECRT_H

typedef unsigned char		u_char;
typedef unsigned short		u_short;
typedef unsigned short		u_int16;
typedef unsigned long		u_long;
typedef short int			int16;
#define PF_TABLE_BITS	12
#define PF_TABLE_SZ_16	4096

#define PF_DEEP_COLOR_AWARE 1

#include "AEConfig.h"

#ifdef AE_OS_WIN
	typedef unsigned short PixelType;
	#include <Windows.h>
#endif

#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

#include "BlazeCRT_Strings.h"
#include "BlazeCRT_SIMD.h"

/* ═══════════════════════════════════════════════════════════════
   Versioning — v3.0 Complete Edition
   ═══════════════════════════════════════════════════════════════ */

#define MAJOR_VERSION   3
#define MINOR_VERSION   0
#define BUG_VERSION     0
#define STAGE_VERSION   PF_Stage_DEVELOP
#define BUILD_VERSION   1

/* ═══════════════════════════════════════════════════════════════
   Parameter Indices — organized as TUBE / SIGNAL / POST
   ═══════════════════════════════════════════════════════════════ */
enum {
	BLAZECRT_INPUT = 0,

	/* ── TUBE: Geometry & Mask ─────────────────────────────── */
	BLAZECRT_TUBE_START,
	BLAZECRT_CURVATURE,
	BLAZECRT_PHOSPHOR_MODE,        /* Off | Aperture Grille | Shadow Mask */
	BLAZECRT_PHOSPHOR_INTENSITY,
	BLAZECRT_TUBE_END,

	/* ── SIGNAL: Scanlines, Flicker, Hum Bar, Interlace ──── */
	BLAZECRT_SIGNAL_START,
	BLAZECRT_SCANLINE_OPACITY,
	BLAZECRT_SCANLINE_FREQ,
	BLAZECRT_SCANLINE_SOFT,
	BLAZECRT_SCANLINE_PHASE,       /* Angle dial */
	BLAZECRT_SCANLINE_ROTATION,    /* Angle dial */
	BLAZECRT_SCANLINE_ANCHOR,      /* Point control */
	BLAZECRT_FLICKER_AMOUNT,
	BLAZECRT_FLICKER_SPEED,
	BLAZECRT_HUM_INTENSITY,
	BLAZECRT_HUM_SPEED,
	BLAZECRT_HUM_WIDTH,
	BLAZECRT_INTERLACE_ON,         /* Checkbox */
	BLAZECRT_INTERLACE_SHIFT,
	BLAZECRT_NOISE_AMOUNT,
	BLAZECRT_SIGNAL_END,

	/* ── POST: Bloom, Color, Vignette, Grain ─────────────── */
	BLAZECRT_POST_START,
	BLAZECRT_RGB_AMOUNT,
	BLAZECRT_RGB_MODE,             /* Subpixel | Horizontal | Radial */
	BLAZECRT_CHROM_ABB,
	BLAZECRT_BLOOM_AMOUNT,
	BLAZECRT_BLOOM_HQ,            /* Checkbox */
	BLAZECRT_COLOR_TEMP,
	BLAZECRT_VIGNETTE,
	BLAZECRT_DECAY,
	BLAZECRT_GRAIN_AMOUNT,
	BLAZECRT_GRAIN_SIZE,
	BLAZECRT_POST_END,

	BLAZECRT_NUM_PARAMS
};

/* ═══════════════════════════════════════════════════════════════
   Disk IDs — stable, never change after shipping
   ═══════════════════════════════════════════════════════════════ */
enum {
	/* TUBE */
	TUBE_START_DISK_ID = 1,
	CURVATURE_DISK_ID,
	PHOSPHOR_MODE_DISK_ID,
	PHOSPHOR_INTENSITY_DISK_ID,
	TUBE_END_DISK_ID,

	/* SIGNAL */
	SIGNAL_START_DISK_ID,
	SCANLINE_OPACITY_DISK_ID,
	SCANLINE_FREQ_DISK_ID,
	SCANLINE_SOFT_DISK_ID,
	SCANLINE_PHASE_DISK_ID,
	SCANLINE_ROTATION_DISK_ID,
	SCANLINE_ANCHOR_DISK_ID,
	FLICKER_AMOUNT_DISK_ID,
	FLICKER_SPEED_DISK_ID,
	HUM_INTENSITY_DISK_ID,
	HUM_SPEED_DISK_ID,
	HUM_WIDTH_DISK_ID,
	INTERLACE_ON_DISK_ID,
	INTERLACE_SHIFT_DISK_ID,
	NOISE_AMOUNT_DISK_ID,
	SIGNAL_END_DISK_ID,

	/* POST */
	POST_START_DISK_ID,
	RGB_AMOUNT_DISK_ID,
	RGB_MODE_DISK_ID,
	CHROM_ABB_DISK_ID,
	BLOOM_AMOUNT_DISK_ID,
	BLOOM_HQ_DISK_ID,
	COLOR_TEMP_DISK_ID,
	VIGNETTE_DISK_ID,
	DECAY_DISK_ID,
	GRAIN_AMOUNT_DISK_ID,
	GRAIN_SIZE_DISK_ID,
	POST_END_DISK_ID
};

/* ═══════════════════════════════════════════════════════════════
   CRTInfo — all render state, passed to row processors
   Must be READ-ONLY during multi-threaded render
   ═══════════════════════════════════════════════════════════════ */
typedef struct CRTInfo {
	/* ── TUBE params ── */
	float curvature;
	int   phosphor_mode;       /* 0=off, 1=aperture grille, 2=shadow mask */
	float phosphor_intensity;

	/* ── SIGNAL params ── */
	float scanline_op;
	float scanline_freq;
	float scanline_soft;
	float scanline_phase;
	float scanline_rotation;
	float anchor_x, anchor_y;
	float flicker_amount;
	float flicker_speed;
	float hum_intensity;
	float hum_speed;
	float hum_width;
	int   interlace_on;
	float interlace_shift;
	float noise_amount;

	/* ── POST params ── */
	float rgb_amt;
	int   rgb_mode;
	float chrom_abb;
	float bloom_amt;
	int   bloom_hq;
	float color_temp;
	float vignette;
	float decay;
	float grain_amt;
	int   grain_size;

	/* ── Frame info ── */
	int   width;
	int   height;
	A_long rowbytes_in;
	A_long rowbytes_out;
	float time;
	unsigned int frame_count;

	/* ── Pre-calculated per-frame values (no trig in hot loop) ── */
	float flicker_mult;    /* Pre-calculated flicker for this frame */
	float color_temp_r;    /* R channel multiplier for color temp */
	float color_temp_b;    /* B channel multiplier for color temp */
	float scan_sin;        /* Pre-calculated sin(rotation) */
	float scan_cos;        /* Pre-calculated cos(rotation) */

	/* ── Buffer pointers (read-only) ── */
	void* in_data_ptr;
} CRTInfo;

extern "C" {
	DllExport
	PF_Err
	EffectMain(
		PF_Cmd			cmd,
		PF_InData		*in_data,
		PF_OutData		*out_data,
		PF_ParamDef		*params[],
		PF_LayerDef		*output,
		void			*extra);
}

#endif /* BLAZECRT_H */