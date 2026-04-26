#include "BlazeCRT.h"

static PF_Err 
About (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	
	suites.ANSICallbacksSuite1()->sprintf(
        out_data->return_msg,
        "%s v%d.%d\r%s",
        "BlazeCRT",
        MAJOR_VERSION,
        MINOR_VERSION,
        "CPU Accelerated CRT Effect by Blaze");
        
	return PF_Err_NONE;
}

static PF_Err 
GlobalSetup (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	out_data->my_version = PF_VERSION(	MAJOR_VERSION, 
										MINOR_VERSION,
										BUG_VERSION, 
										STAGE_VERSION, 
										BUILD_VERSION);

	out_data->out_flags =  PF_OutFlag_DEEP_COLOR_AWARE;	
	
	return PF_Err_NONE;
}

static PF_Err 
ParamsSetup (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	PF_Err		err		= PF_Err_NONE;
	PF_ParamDef	def;	

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(	"Scanline Opacity", 
							BLAZECRT_SCANLINE_MIN, 
							BLAZECRT_SCANLINE_MAX, 
							BLAZECRT_SCANLINE_MIN, 
							BLAZECRT_SCANLINE_MAX, 
							BLAZECRT_SCANLINE_DFLT,
							PF_Precision_HUNDREDTHS,
							0, 0, SCANLINE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(	"RGB Split Amount", 
							BLAZECRT_RGB_SPLIT_MIN, 
							BLAZECRT_RGB_SPLIT_MAX, 
							BLAZECRT_RGB_SPLIT_MIN, 
							BLAZECRT_RGB_SPLIT_MAX, 
							BLAZECRT_RGB_SPLIT_DFLT,
							PF_Precision_HUNDREDTHS,
							0, 0, RGB_DISK_ID);
	
	out_data->num_params = BLAZECRT_NUM_PARAMS;

	return err;
}

static PF_Err
CRTRenderFunc8 (
	void		*refcon, 
	A_long		xL, 
	A_long		yL, 
	PF_Pixel8	*inP, 
	PF_Pixel8	*outP)
{
	PF_Err		err = PF_Err_NONE;
	CRTInfo	*giP = reinterpret_cast<CRTInfo*>(refcon);
					
	if (giP){
		double scanlineOp = giP->scanlineF / 100.0;
		double rgbAmt = giP->rgbF / 100.0;

		double rMultiplier = 1.0;
		double gMultiplier = 1.0;
		double bMultiplier = 1.0;

		// RGB Subpixels (every 3 pixels)
		int xMod = xL % 3;
		if (xMod == 0) { rMultiplier = 1.0; gMultiplier = 1.0 - (0.5 * rgbAmt); bMultiplier = 1.0 - (0.5 * rgbAmt); }
		else if (xMod == 1) { rMultiplier = 1.0 - (0.5 * rgbAmt); gMultiplier = 1.0; bMultiplier = 1.0 - (0.5 * rgbAmt); }
		else { rMultiplier = 1.0 - (0.5 * rgbAmt); gMultiplier = 1.0 - (0.5 * rgbAmt); bMultiplier = 1.0; }

		// Scanlines (darken every 3rd row)
		int yMod = yL % 3;
		double scanMult = 1.0;
		if (yMod == 0) {
			scanMult = 1.0 - (0.6 * scanlineOp);
		}

		outP->alpha	= inP->alpha;
		outP->red	= MIN((inP->red * rMultiplier * scanMult), PF_MAX_CHAN8);
		outP->green	= MIN((inP->green * gMultiplier * scanMult), PF_MAX_CHAN8);
		outP->blue	= MIN((inP->blue * bMultiplier * scanMult), PF_MAX_CHAN8);
	}
	return err;
}

static PF_Err
CRTRenderFunc16 (
	void		*refcon, 
	A_long		xL, 
	A_long		yL, 
	PF_Pixel16	*inP, 
	PF_Pixel16	*outP)
{
	PF_Err		err = PF_Err_NONE;
	CRTInfo	*giP = reinterpret_cast<CRTInfo*>(refcon);
					
	if (giP){
		double scanlineOp = giP->scanlineF / 100.0;
		double rgbAmt = giP->rgbF / 100.0;

		double rMultiplier = 1.0;
		double gMultiplier = 1.0;
		double bMultiplier = 1.0;

		// RGB Subpixels (every 3 pixels)
		int xMod = xL % 3;
		if (xMod == 0) { rMultiplier = 1.0; gMultiplier = 1.0 - (0.5 * rgbAmt); bMultiplier = 1.0 - (0.5 * rgbAmt); }
		else if (xMod == 1) { rMultiplier = 1.0 - (0.5 * rgbAmt); gMultiplier = 1.0; bMultiplier = 1.0 - (0.5 * rgbAmt); }
		else { rMultiplier = 1.0 - (0.5 * rgbAmt); gMultiplier = 1.0 - (0.5 * rgbAmt); bMultiplier = 1.0; }

		// Scanlines (darken every 3rd row)
		int yMod = yL % 3;
		double scanMult = 1.0;
		if (yMod == 0) {
			scanMult = 1.0 - (0.6 * scanlineOp);
		}

		outP->alpha	= inP->alpha;
		outP->red	= MIN((inP->red * rMultiplier * scanMult), PF_MAX_CHAN16);
		outP->green	= MIN((inP->green * gMultiplier * scanMult), PF_MAX_CHAN16);
		outP->blue	= MIN((inP->blue * bMultiplier * scanMult), PF_MAX_CHAN16);
	}
	return err;
}


static PF_Err 
Render (
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	PF_Err				err		= PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);

	CRTInfo giP;
	AEFX_CLR_STRUCT(giP);
	A_long linesL = output->extent_hint.bottom - output->extent_hint.top;

	giP.scanlineF = params[BLAZECRT_SCANLINE_AMOUNT]->u.fs_d.value;
	giP.rgbF = params[BLAZECRT_RGB_AMOUNT]->u.fs_d.value;
	
	if (PF_WORLD_IS_DEEP(output)){
		ERR(suites.Iterate16Suite2()->iterate(	in_data,
												0,								
												linesL,							
												&params[BLAZECRT_INPUT]->u.ld,	
												NULL,							
												(void*)&giP,					
												CRTRenderFunc16,				
												output));
	} else {
		ERR(suites.Iterate8Suite2()->iterate(	in_data,
												0,								
												linesL,							
												&params[BLAZECRT_INPUT]->u.ld,	
												NULL,							
												(void*)&giP,					
												CRTRenderFunc8,				
												output));	
	}

	return err;
}


extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr inPtr,
	PF_PluginDataCB2 inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr,
	const char* inHostName,
	const char* inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;

	result = PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		"BlazeCRT", // Name
		"ADBE BlazeCRT", // Match Name
		"Blaze Plugins", // Category
		AE_RESERVED_INFO, // Reserved Info
		"EffectMain",	// Entry point
		"https://github.com/blazethetechguy/BlazeCRT");	// support URL

	return result;
}


PF_Err
EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra)
{
	PF_Err		err = PF_Err_NONE;
	
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:
				err = About(in_data, out_data, params, output);
				break;
			case PF_Cmd_GLOBAL_SETUP:
				err = GlobalSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_PARAMS_SETUP:
				err = ParamsSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_RENDER:
				err = Render(in_data, out_data, params, output);
				break;
		}
	}
	catch(PF_Err &thrown_err){
		err = thrown_err;
	}
	return err;
}
