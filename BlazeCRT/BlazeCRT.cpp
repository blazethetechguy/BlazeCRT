#include "BlazeCRT.h"
#include <math.h>

/* --------------------------------------------------------
   Inline helpers
   -------------------------------------------------------- */

static inline float clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static inline PF_Pixel8 sample8(const CRTInfo* gi, float sx, float sy) {
	int ix = (int)sx;
	int iy = (int)sy;
	if (ix < 0) ix = 0; else if (ix >= gi->width)  ix = gi->width  - 1;
	if (iy < 0) iy = 0; else if (iy >= gi->height) iy = gi->height - 1;
	return *((PF_Pixel8*)((char*)gi->in_data8 + (A_long)iy * gi->rowbytes_in) + ix);
}

static inline PF_Pixel16 sample16(const CRTInfo* gi, float sx, float sy) {
	int ix = (int)sx;
	int iy = (int)sy;
	if (ix < 0) ix = 0; else if (ix >= gi->width)  ix = gi->width  - 1;
	if (iy < 0) iy = 0; else if (iy >= gi->height) iy = gi->height - 1;
	return *((PF_Pixel16*)((char*)gi->in_data16 + (A_long)iy * gi->rowbytes_in) + ix);
}

static inline unsigned int fast_hash(unsigned int x, unsigned int y, unsigned int frame) {
	unsigned int seed = x * 1234567u + y * 7654321u + frame * 1013904223u;
	seed ^= (seed << 13);
	seed ^= (seed >> 17);
	seed ^= (seed << 5);
	return seed;
}

/* --------------------------------------------------------
   Per-pixel render – 8-bit
   Called by AE's Iterate8Suite on multiple threads
   -------------------------------------------------------- */
static PF_Err
CRTRenderFunc8(void* refcon, A_long xL, A_long yL, PF_Pixel8* inP, PF_Pixel8* outP)
{
	const CRTInfo* gi = (const CRTInfo*)refcon;
	const float W = (float)gi->width;
	const float H = (float)gi->height;
	const float x = (float)xL;
	const float y = (float)yL;

	/* Normalized coords [-1,1] */
	float nx = (2.0f * x / W) - 1.0f;
	float ny = (2.0f * y / H) - 1.0f;
	float r2 = nx * nx + ny * ny;

	/* --- Barrel curvature: find source sample position --- */
	float srcX = x, srcY = y;
	if (gi->curvature_amt > 0.0f) {
		float k = gi->curvature_amt * 0.25f;
		float distort = 1.0f + k * r2;
		srcX = (nx * distort + 1.0f) * 0.5f * W;
		srcY = (ny * distort + 1.0f) * 0.5f * H;
		if (srcX < 0.0f || srcX >= W || srcY < 0.0f || srcY >= H) {
			outP->alpha = 0; outP->red = 0; outP->green = 0; outP->blue = 0;
			return PF_Err_NONE;
		}
	}

	/* --- Chromatic aberration: separate R / B sample offsets --- */
	float caOff = gi->chrom_abb * r2 * 18.0f;
	PF_Pixel8 pC = sample8(gi, srcX, srcY);
	PF_Pixel8 pR = (gi->chrom_abb > 0.0f) ? sample8(gi, srcX + nx * caOff, srcY + ny * caOff) : pC;
	PF_Pixel8 pB = (gi->chrom_abb > 0.0f) ? sample8(gi, srcX - nx * caOff, srcY - ny * caOff) : pC;

	float fR = (float)pR.red;
	float fG = (float)pC.green;
	float fB = (float)pB.blue;
	float fA = (float)pC.alpha;

	/* --- RGB split --- */
	if (gi->rgb_amt > 0.0f) {
		const float amt = gi->rgb_amt;
		if (gi->rgb_mode == 0) { /* Subpixel */
			int xMod = xL % 3;
			if      (xMod == 0) { fG *= (1.0f - 0.5f * amt); fB *= (1.0f - 0.5f * amt); }
			else if (xMod == 1) { fR *= (1.0f - 0.5f * amt); fB *= (1.0f - 0.5f * amt); }
			else                { fR *= (1.0f - 0.5f * amt); fG *= (1.0f - 0.5f * amt); }
		} else if (gi->rgb_mode == 1) { /* Horizontal */
			float off = amt * 4.0f;
			fR = (float)sample8(gi, srcX + off, srcY).red;
			fB = (float)sample8(gi, srcX - off, srcY).blue;
		} else { /* Radial */
			float off = amt * r2 * 12.0f;
			fR = (float)sample8(gi, srcX + nx * off, srcY + ny * off).red;
			fB = (float)sample8(gi, srcX - nx * off, srcY - ny * off).blue;
		}
	}

	/* --- Scanlines --- */
	if (gi->scanline_op > 0.0f) {
		float freq = gi->scanline_freq < 1.0f ? 1.0f : gi->scanline_freq;
		float val = y + gi->time * gi->scanline_speed * 20.0f;
		float phase = fmodf(val, freq) / freq;
		if (phase < 0.0f) phase += 1.0f;
		float hard  = (phase < 0.5f) ? 1.0f : 0.0f;
		float soft  = cosf(phase * 6.28318f) * 0.5f + 0.5f;
		float pattern  = hard * (1.0f - gi->scanline_soft) + soft * gi->scanline_soft;
		float scanMult = 1.0f - gi->scanline_op * (1.0f - pattern);
		fR *= scanMult; fG *= scanMult; fB *= scanMult;
	}

	/* --- Vignette --- */
	if (gi->vignette_amt > 0.0f) {
		float vig = clampf(1.0f - gi->vignette_amt * r2 * 1.5f, 0.0f, 1.0f);
		fR *= vig; fG *= vig; fB *= vig;
	}

	/* --- Phosphor bloom (additive, reads from input) --- */
	if (gi->bloom_amt > 0.0f) {
		float bR = 0.0f, bG = 0.0f, bB = 0.0f, wSum = 0.0f;
		int radius = gi->bloom_hq ? 3 : 1;
		float spread = gi->bloom_hq ? 2.5f : 1.5f;
		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
				float w = expf(-(float)(dx*dx + dy*dy) / (2.0f * spread * spread));
				PF_Pixel8 bp = sample8(gi, srcX + (float)dx * 2.0f, srcY + (float)dy * 2.0f);
				bR += (float)bp.red   * w;
				bG += (float)bp.green * w;
				bB += (float)bp.blue  * w;
				wSum += w;
			}
		}
		float scale = gi->bloom_amt * 0.5f / wSum;
		fR += bR * scale; fG += bG * scale; fB += bB * scale;
	}

	/* --- Film grain (per-block hash, frame-seeded) --- */
	if (gi->grain_amt > 0.0f) {
		int gs = gi->grain_size < 1 ? 1 : gi->grain_size;
		unsigned int h = fast_hash((unsigned int)((int)srcX / gs),
		                           (unsigned int)((int)srcY / gs),
		                           gi->frame_count);
		float noise = ((float)(h & 0xFFFF) / 65535.0f) - 0.5f;
		float nf = 1.0f + noise * gi->grain_amt * 0.8f;
		fR *= nf; fG *= nf; fB *= nf;
	}

	outP->alpha = (A_u_char)fA;
	outP->red   = (A_u_char)(A_u_char)clampf(fR, 0.0f, (float)PF_MAX_CHAN8);
	outP->green = (A_u_char)(A_u_char)clampf(fG, 0.0f, (float)PF_MAX_CHAN8);
	outP->blue  = (A_u_char)(A_u_char)clampf(fB, 0.0f, (float)PF_MAX_CHAN8);
	return PF_Err_NONE;
}

/* --------------------------------------------------------
   Per-pixel render – 16-bit
   -------------------------------------------------------- */
static PF_Err
CRTRenderFunc16(void* refcon, A_long xL, A_long yL, PF_Pixel16* inP, PF_Pixel16* outP)
{
	const CRTInfo* gi = (const CRTInfo*)refcon;
	const float W  = (float)gi->width;
	const float H  = (float)gi->height;
	const float x  = (float)xL;
	const float y  = (float)yL;
	const float MAX = (float)PF_MAX_CHAN16;

	float nx = (2.0f * x / W) - 1.0f;
	float ny = (2.0f * y / H) - 1.0f;
	float r2 = nx * nx + ny * ny;

	float srcX = x, srcY = y;
	if (gi->curvature_amt > 0.0f) {
		float distort = 1.0f + gi->curvature_amt * 0.25f * r2;
		srcX = (nx * distort + 1.0f) * 0.5f * W;
		srcY = (ny * distort + 1.0f) * 0.5f * H;
		if (srcX < 0.0f || srcX >= W || srcY < 0.0f || srcY >= H) {
			outP->alpha = 0; outP->red = 0; outP->green = 0; outP->blue = 0;
			return PF_Err_NONE;
		}
	}

	float caOff = gi->chrom_abb * r2 * 18.0f;
	PF_Pixel16 pC = sample16(gi, srcX, srcY);
	PF_Pixel16 pR = (gi->chrom_abb > 0.0f) ? sample16(gi, srcX + nx * caOff, srcY + ny * caOff) : pC;
	PF_Pixel16 pB = (gi->chrom_abb > 0.0f) ? sample16(gi, srcX - nx * caOff, srcY - ny * caOff) : pC;

	float fR = (float)pR.red;
	float fG = (float)pC.green;
	float fB = (float)pB.blue;
	float fA = (float)pC.alpha;

	if (gi->rgb_amt > 0.0f) {
		const float amt = gi->rgb_amt;
		if (gi->rgb_mode == 0) {
			int xMod = xL % 3;
			if      (xMod == 0) { fG *= (1.0f - 0.5f * amt); fB *= (1.0f - 0.5f * amt); }
			else if (xMod == 1) { fR *= (1.0f - 0.5f * amt); fB *= (1.0f - 0.5f * amt); }
			else                { fR *= (1.0f - 0.5f * amt); fG *= (1.0f - 0.5f * amt); }
		} else if (gi->rgb_mode == 1) {
			float off = amt * 4.0f;
			fR = (float)sample16(gi, srcX + off, srcY).red;
			fB = (float)sample16(gi, srcX - off, srcY).blue;
		} else {
			float off = amt * r2 * 12.0f;
			fR = (float)sample16(gi, srcX + nx * off, srcY + ny * off).red;
			fB = (float)sample16(gi, srcX - nx * off, srcY - ny * off).blue;
		}
	}

	if (gi->scanline_op > 0.0f) {
		float freq  = gi->scanline_freq < 1.0f ? 1.0f : gi->scanline_freq;
		float val = y + gi->time * gi->scanline_speed * 20.0f;
		float phase = fmodf(val, freq) / freq;
		if (phase < 0.0f) phase += 1.0f;
		float hard  = (phase < 0.5f) ? 1.0f : 0.0f;
		float soft  = cosf(phase * 6.28318f) * 0.5f + 0.5f;
		float scanMult = 1.0f - gi->scanline_op * (1.0f - (hard * (1.0f - gi->scanline_soft) + soft * gi->scanline_soft));
		fR *= scanMult; fG *= scanMult; fB *= scanMult;
	}

	if (gi->vignette_amt > 0.0f) {
		float vig = clampf(1.0f - gi->vignette_amt * r2 * 1.5f, 0.0f, 1.0f);
		fR *= vig; fG *= vig; fB *= vig;
	}

	if (gi->bloom_amt > 0.0f) {
		float bR = 0.0f, bG = 0.0f, bB = 0.0f, wSum = 0.0f;
		int radius = gi->bloom_hq ? 3 : 1;
		float spread = gi->bloom_hq ? 2.5f : 1.5f;
		for (int dy = -radius; dy <= radius; dy++) {
			for (int dx = -radius; dx <= radius; dx++) {
				float w = expf(-(float)(dx*dx + dy*dy) / (2.0f * spread * spread));
				PF_Pixel16 bp = sample16(gi, srcX + (float)dx * 2.0f, srcY + (float)dy * 2.0f);
				bR += (float)bp.red   * w;
				bG += (float)bp.green * w;
				bB += (float)bp.blue  * w;
				wSum += w;
			}
		}
		float scale = gi->bloom_amt * 0.5f / wSum;
		fR += bR * scale; fG += bG * scale; fB += bB * scale;
	}

	if (gi->grain_amt > 0.0f) {
		int gs = gi->grain_size < 1 ? 1 : gi->grain_size;
		unsigned int h = fast_hash((unsigned int)((int)srcX / gs),
		                           (unsigned int)((int)srcY / gs),
		                           gi->frame_count);
		float noise = ((float)(h & 0xFFFF) / 65535.0f) - 0.5f;
		float nf = 1.0f + noise * gi->grain_amt * 0.8f;
		fR *= nf; fG *= nf; fB *= nf;
	}

	outP->alpha = (A_u_short)fA;
	outP->red   = (A_u_short)clampf(fR, 0.0f, MAX);
	outP->green = (A_u_short)clampf(fG, 0.0f, MAX);
	outP->blue  = (A_u_short)clampf(fB, 0.0f, MAX);
	return PF_Err_NONE;
}

/* --------------------------------------------------------
   AE Command Handlers
   -------------------------------------------------------- */
static PF_Err
About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
		"%s v%d.%d\r%s", "BlazeCRT", MAJOR_VERSION, MINOR_VERSION,
		"Multi-threaded CRT Suite by Blaze");
	return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
	out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
	/* PIX_INDEPENDENT = each pixel independent → AE enables max threading via Iterate */
	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |
	                       PF_OutFlag_PIX_INDEPENDENT  |
	                       PF_OutFlag_NON_PARAM_VARY;    /* grain varies every frame */
	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
	return PF_Err_NONE;
}

static PF_Err
ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
	PF_Err err = PF_Err_NONE;
	PF_ParamDef def;

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Scanline Opacity",  0, 100, 0, 100, 50,  PF_Precision_HUNDREDTHS, 0, 0, SCANLINE_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Scanline Frequency",1,   8, 1,   8,  3,  PF_Precision_HUNDREDTHS, 0, 0, SCANLINE_FREQ_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Scanline Softness", 0, 100, 0, 100, 20,  PF_Precision_HUNDREDTHS, 0, 0, SCANLINE_SOFT_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Scanline Speed",  -100, 100, -100, 100, 0,  PF_Precision_HUNDREDTHS, 0, 0, SCANLINE_SPEED_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("RGB Split Amount",  0, 100, 0, 100, 50,  PF_Precision_HUNDREDTHS, 0, 0, RGB_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("RGB Split Mode", 3, 1, "Subpixel|Horizontal|Radial", RGB_MODE_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Chromatic Aberration", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, CHROM_ABB_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Film Grain Amount", 0, 100, 0, 100, 0,   PF_Precision_HUNDREDTHS, 0, 0, GRAIN_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Grain Size", 1, 8, 1, 8, 1, GRAIN_SIZE_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Phosphor Bloom",    0, 100, 0, 100, 0,   PF_Precision_HUNDREDTHS, 0, 0, BLOOM_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOX("High Quality Bloom", "On", 0, 0, BLOOM_HQ_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Vignette Amount",   0, 100, 0, 100, 0,   PF_Precision_HUNDREDTHS, 0, 0, VIGNETTE_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Curvature Amount",  0, 100, 0, 100, 0,   PF_Precision_HUNDREDTHS, 0, 0, CURVATURE_DISK_ID);

	out_data->num_params = BLAZECRT_NUM_PARAMS;
	return err;
}

static PF_Err
Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	CRTInfo gi;
	AEFX_CLR_STRUCT(gi);
	A_long linesL = output->extent_hint.bottom - output->extent_hint.top;

	gi.scanline_op   = (float)(params[BLAZECRT_SCANLINE_AMOUNT]->u.fs_d.value / 100.0);
	gi.scanline_freq = (float)(params[BLAZECRT_SCANLINE_FREQ]->u.fs_d.value);
	gi.scanline_soft = (float)(params[BLAZECRT_SCANLINE_SOFT]->u.fs_d.value / 100.0);
	gi.scanline_speed= (float)(params[BLAZECRT_SCANLINE_SPEED]->u.fs_d.value);
	gi.rgb_amt       = (float)(params[BLAZECRT_RGB_AMOUNT]->u.fs_d.value / 100.0);
	gi.rgb_mode      = params[BLAZECRT_RGB_MODE]->u.pd.value - 1;
	gi.chrom_abb     = (float)(params[BLAZECRT_CHROM_ABB]->u.fs_d.value / 100.0);
	gi.grain_amt     = (float)(params[BLAZECRT_GRAIN_AMOUNT]->u.fs_d.value / 100.0);
	gi.grain_size    = params[BLAZECRT_GRAIN_SIZE]->u.sd.value;
	gi.bloom_amt     = (float)(params[BLAZECRT_BLOOM_AMOUNT]->u.fs_d.value / 100.0);
	gi.bloom_hq      = params[BLAZECRT_BLOOM_HQ]->u.bd.value;
	gi.vignette_amt  = (float)(params[BLAZECRT_VIGNETTE_AMOUNT]->u.fs_d.value / 100.0);
	gi.curvature_amt = (float)(params[BLAZECRT_CURVATURE_AMOUNT]->u.fs_d.value / 100.0);

	gi.width       = output->width;
	gi.height      = output->height;
	gi.rowbytes_in = params[BLAZECRT_INPUT]->u.ld.rowbytes;
	gi.time        = (float)((double)in_data->current_time / (double)in_data->time_scale);
	gi.frame_count = (unsigned int)(in_data->current_time / in_data->time_step);
	gi.in_data8    = (void*)params[BLAZECRT_INPUT]->u.ld.data;
	gi.in_data16   = (void*)params[BLAZECRT_INPUT]->u.ld.data;

	if (PF_WORLD_IS_DEEP(output)) {
		ERR(suites.Iterate16Suite2()->iterate(in_data, 0, linesL,
			&params[BLAZECRT_INPUT]->u.ld, NULL,
			(void*)&gi, CRTRenderFunc16, output));
	} else {
		ERR(suites.Iterate8Suite2()->iterate(in_data, 0, linesL,
			&params[BLAZECRT_INPUT]->u.ld, NULL,
			(void*)&gi, CRTRenderFunc8, output));
	}
	return err;
}

/* --------------------------------------------------------
   Plugin registration
   -------------------------------------------------------- */
extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr, const char* inHostName, const char* inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;
	result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr,
		"BlazeCRT", "ADBE BlazeCRT", "Blaze Plugins",
		AE_RESERVED_INFO, "EffectMain",
		"https://github.com/blazethetechguy/BlazeCRT");
	return result;
}

PF_Err
EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
           PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:        err = About(in_data, out_data, params, output);       break;
			case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_RENDER:       err = Render(in_data, out_data, params, output);      break;
		}
	}
	catch (PF_Err& thrown_err) { err = thrown_err; }
	return err;
}
