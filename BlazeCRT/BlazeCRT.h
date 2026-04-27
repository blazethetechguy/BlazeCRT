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

/* Versioning information */

#define	MAJOR_VERSION	2
#define	MINOR_VERSION	1
#define	BUG_VERSION		0
#define	STAGE_VERSION	PF_Stage_DEVELOP
#define	BUILD_VERSION	1

/* Parameter indices */
enum {
	BLAZECRT_INPUT = 0,
	BLAZECRT_SCANLINE_AMOUNT,
	BLAZECRT_SCANLINE_FREQ,
	BLAZECRT_SCANLINE_SOFT,
	BLAZECRT_RGB_AMOUNT,
	BLAZECRT_RGB_MODE,
	BLAZECRT_CHROM_ABB,
	BLAZECRT_GRAIN_AMOUNT,
	BLAZECRT_GRAIN_SIZE,
	BLAZECRT_BLOOM_AMOUNT,
	BLAZECRT_BLOOM_HQ,
	BLAZECRT_VIGNETTE_AMOUNT,
	BLAZECRT_CURVATURE_AMOUNT,
	BLAZECRT_NUM_PARAMS
};

/* Disk IDs (stable, never change these) */
enum {
	SCANLINE_DISK_ID = 1,
	SCANLINE_FREQ_DISK_ID,
	SCANLINE_SOFT_DISK_ID,
	RGB_DISK_ID,
	RGB_MODE_DISK_ID,
	CHROM_ABB_DISK_ID,
	GRAIN_DISK_ID,
	GRAIN_SIZE_DISK_ID,
	BLOOM_DISK_ID,
	BLOOM_HQ_DISK_ID,
	VIGNETTE_DISK_ID,
	CURVATURE_DISK_ID
};

/* Refcon passed to iterate callbacks - must be READ-ONLY during render */
typedef struct CRTInfo {
	/* Effect parameters */
	float scanline_op;
	float scanline_freq;
	float scanline_soft;
	float rgb_amt;
	int   rgb_mode;
	float chrom_abb;
	float grain_amt;
	int   grain_size;
	float bloom_amt;
	int   bloom_hq;
	float vignette_amt;
	float curvature_amt;

	/* Frame info */
	int   width;
	int   height;
	A_long rowbytes_in;
	unsigned int frame_count;

	/* Pointer to full input layer (for neighbor sampling) */
	void* in_data8;
	void* in_data16;
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

#endif // BLAZECRT_H