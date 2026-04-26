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

#define	MAJOR_VERSION	1
#define	MINOR_VERSION	0
#define	BUG_VERSION		0
#define	STAGE_VERSION	PF_Stage_DEVELOP
#define	BUILD_VERSION	1


/* Parameter defaults */

#define	BLAZECRT_SCANLINE_MIN		0
#define	BLAZECRT_SCANLINE_MAX		100
#define	BLAZECRT_SCANLINE_DFLT		50

#define BLAZECRT_RGB_SPLIT_MIN      0
#define BLAZECRT_RGB_SPLIT_MAX      100
#define BLAZECRT_RGB_SPLIT_DFLT     50

enum {
	BLAZECRT_INPUT = 0,
	BLAZECRT_SCANLINE_AMOUNT,
	BLAZECRT_RGB_AMOUNT,
	BLAZECRT_NUM_PARAMS
};

enum {
	SCANLINE_DISK_ID = 1,
	RGB_DISK_ID,
};

typedef struct CRTInfo{
	PF_FpLong	scanlineF;
	PF_FpLong	rgbF;
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