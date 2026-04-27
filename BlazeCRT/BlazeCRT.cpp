#include "BlazeCRT.h"
#include <math.h>
#include <omp.h>

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static inline PF_Pixel8 sample8(const CRTInfo* gi, float sx, float sy) {
	int ix = (int)sx, iy = (int)sy;
	if (ix < 0) ix = 0; else if (ix >= gi->width)  ix = gi->width  - 1;
	if (iy < 0) iy = 0; else if (iy >= gi->height) iy = gi->height - 1;
	return *((PF_Pixel8*)((char*)gi->in_data_ptr + (A_long)iy * gi->rowbytes_in) + ix);
}

static inline PF_Pixel16 sample16(const CRTInfo* gi, float sx, float sy) {
	int ix = (int)sx, iy = (int)sy;
	if (ix < 0) ix = 0; else if (ix >= gi->width)  ix = gi->width  - 1;
	if (iy < 0) iy = 0; else if (iy >= gi->height) iy = gi->height - 1;
	return *((PF_Pixel16*)((char*)gi->in_data_ptr + (A_long)iy * gi->rowbytes_in) + ix);
}

static void ProcessRow8(const CRTInfo* gi, int y) {
	float W = (float)gi->width, H = (float)gi->height;
	PF_Pixel8* out_row = (PF_Pixel8*)((char*)gi->out_data_ptr + y * gi->rowbytes_out);
	int src_y = y;
	if (gi->interlace_on && (y + gi->frame_count) % 2 == 0) src_y += (int)gi->interlace_shift;
	
	for (int x = 0; x < gi->width; x++) {
		float nx = (2.0f * x / W) - 1.0f, ny = (2.0f * y / H) - 1.0f;
		float r2 = nx * nx + ny * ny;
		float srcX = (float)x, srcY = (float)src_y;
		
		if (gi->curvature > 0.0f) {
			float distort = 1.0f + gi->curvature * 0.25f * r2;
			srcX = (nx * distort + 1.0f) * 0.5f * W;
			srcY = (ny * distort + 1.0f) * 0.5f * H;
			if (srcX < 0.0f || srcX >= W || srcY < 0.0f || srcY >= H) {
				out_row[x].alpha = 0; out_row[x].red = 0; out_row[x].green = 0; out_row[x].blue = 0;
				continue;
			}
		}

		float caOff = gi->chrom_abb * r2 * 18.0f;
		PF_Pixel8 pC = sample8(gi, srcX, srcY);
		PF_Pixel8 pR = (gi->chrom_abb > 0.0f) ? sample8(gi, srcX + nx * caOff, srcY + ny * caOff) : pC;
		PF_Pixel8 pB = (gi->chrom_abb > 0.0f) ? sample8(gi, srcX - nx * caOff, srcY - ny * caOff) : pC;

		float fR = (float)pR.red, fG = (float)pC.green, fB = (float)pB.blue, fA = (float)pC.alpha;

		if (gi->rgb_amt > 0.0f) {
			float amt = gi->rgb_amt;
			if (gi->rgb_mode == 0) {
				int xMod = x % 3;
				if (xMod == 0) { fG *= (1.0f - 0.5f*amt); fB *= (1.0f - 0.5f*amt); }
				else if (xMod == 1) { fR *= (1.0f - 0.5f*amt); fB *= (1.0f - 0.5f*amt); }
				else { fR *= (1.0f - 0.5f*amt); fG *= (1.0f - 0.5f*amt); }
			} else if (gi->rgb_mode == 1) {
				float off = amt * 4.0f;
				fR = (float)sample8(gi, srcX + off, srcY).red;
				fB = (float)sample8(gi, srcX - off, srcY).blue;
			} else {
				float off = amt * r2 * 12.0f;
				fR = (float)sample8(gi, srcX + nx * off, srcY + ny * off).red;
				fB = (float)sample8(gi, srcX - nx * off, srcY - ny * off).blue;
			}
		}

		__m128 rgba = simd_load_pixel8(fR, fG, fB, fA);
		rgba = simd_mul_channels(rgba, gi->color_temp_r, 1.0f, gi->color_temp_b, 1.0f);

		if (gi->phosphor_mode > 0) {
			if (gi->phosphor_mode == 1) {
				int mx = x % 3;
				if (mx == 0) rgba = simd_mul_channels(rgba, 1.0f, 1.0f - gi->phosphor_intensity, 1.0f - gi->phosphor_intensity, 1.0f);
				else if (mx == 1) rgba = simd_mul_channels(rgba, 1.0f - gi->phosphor_intensity, 1.0f, 1.0f - gi->phosphor_intensity, 1.0f);
				else rgba = simd_mul_channels(rgba, 1.0f - gi->phosphor_intensity, 1.0f - gi->phosphor_intensity, 1.0f, 1.0f);
			} else {
				if ((x + y) % 2 == 0) rgba = simd_mul_scalar(rgba, 1.0f - gi->phosphor_intensity*0.5f);
			}
		}

		if (gi->scanline_op > 0.0f) {
			float freq = gi->scanline_freq < 1.0f ? 1.0f : gi->scanline_freq;
			float dx = x - gi->anchor_x, dy = y - gi->anchor_y;
			float rot_y = dx * gi->scan_sin + dy * gi->scan_cos + gi->anchor_y;
			float val = rot_y + (gi->scanline_phase / 360.0f) * freq;
			float phase = fmodf(val, freq) / freq;
			if (phase < 0.0f) phase += 1.0f;
			float hard = (phase < 0.5f) ? 1.0f : 0.0f;
			float soft = fast_cos(phase * 6.28318f) * 0.5f + 0.5f;
			float pattern = hard * (1.0f - gi->scanline_soft) + soft * gi->scanline_soft;
			rgba = simd_mul_scalar(rgba, 1.0f - gi->scanline_op * (1.0f - pattern));
		}

		if (gi->hum_intensity > 0.0f) {
			float hum_y = y * 0.01f / gi->hum_width + gi->time * gi->hum_speed;
			rgba = simd_mul_scalar(rgba, 1.0f - gi->hum_intensity * (fast_sin(hum_y) * 0.5f + 0.5f));
		}

		if (gi->flicker_amount > 0.0f) rgba = simd_mul_scalar(rgba, gi->flicker_mult);

		if (gi->vignette > 0.0f) {
			float vig = clampf(1.0f - gi->vignette * r2 * 1.5f, 0.0f, 1.0f);
			rgba = simd_mul_scalar(rgba, vig);
		}

		if (gi->noise_amount > 0.0f) {
			unsigned int h = simd_hash((unsigned int)x, (unsigned int)y, gi->frame_count);
			float noise = ((float)(h & 0xFFFF) / 65535.0f) - 0.5f;
			rgba = simd_mul_scalar(rgba, 1.0f + noise * gi->noise_amount);
		}

		rgba = simd_clamp(rgba, (float)PF_MAX_CHAN8);
		fR = simd_extract_r(rgba); fG = simd_extract_g(rgba); fB = simd_extract_b(rgba);
		
		if (gi->bloom_amt > 0.0f) {
			float bR = 0.0f, bG = 0.0f, bB = 0.0f, wSum = 0.0f;
			int radius = gi->bloom_hq ? 3 : 1;
			float spread = gi->bloom_hq ? 2.5f : 1.5f;
			for (int bdy = -radius; bdy <= radius; bdy++) {
				for (int bdx = -radius; bdx <= radius; bdx++) {
					float w = expf(-(float)(bdx*bdx + bdy*bdy) / (2.0f * spread * spread));
					PF_Pixel8 bp = sample8(gi, srcX + (float)bdx * 2.0f, srcY + (float)bdy * 2.0f);
					bR += (float)bp.red * w; bG += (float)bp.green * w; bB += (float)bp.blue * w;
					wSum += w;
				}
			}
			float scale = gi->bloom_amt * 0.5f / wSum;
			fR += bR * scale; fG += bG * scale; fB += bB * scale;
		}

		out_row[x].alpha = (A_u_char)fA;
		out_row[x].red   = (A_u_char)clampf(fR, 0.0f, (float)PF_MAX_CHAN8);
		out_row[x].green = (A_u_char)clampf(fG, 0.0f, (float)PF_MAX_CHAN8);
		out_row[x].blue  = (A_u_char)clampf(fB, 0.0f, (float)PF_MAX_CHAN8);
	}
}

static void ProcessRow16(const CRTInfo* gi, int y) {
	float W = (float)gi->width, H = (float)gi->height;
	PF_Pixel16* out_row = (PF_Pixel16*)((char*)gi->out_data_ptr + y * gi->rowbytes_out);
	int src_y = y;
	if (gi->interlace_on && (y + gi->frame_count) % 2 == 0) src_y += (int)gi->interlace_shift;
	
	for (int x = 0; x < gi->width; x++) {
		float nx = (2.0f * x / W) - 1.0f, ny = (2.0f * y / H) - 1.0f;
		float r2 = nx * nx + ny * ny;
		float srcX = (float)x, srcY = (float)src_y;
		
		if (gi->curvature > 0.0f) {
			float distort = 1.0f + gi->curvature * 0.25f * r2;
			srcX = (nx * distort + 1.0f) * 0.5f * W;
			srcY = (ny * distort + 1.0f) * 0.5f * H;
			if (srcX < 0.0f || srcX >= W || srcY < 0.0f || srcY >= H) {
				out_row[x].alpha = 0; out_row[x].red = 0; out_row[x].green = 0; out_row[x].blue = 0;
				continue;
			}
		}

		float caOff = gi->chrom_abb * r2 * 18.0f;
		PF_Pixel16 pC = sample16(gi, srcX, srcY);
		PF_Pixel16 pR = (gi->chrom_abb > 0.0f) ? sample16(gi, srcX + nx * caOff, srcY + ny * caOff) : pC;
		PF_Pixel16 pB = (gi->chrom_abb > 0.0f) ? sample16(gi, srcX - nx * caOff, srcY - ny * caOff) : pC;

		float fR = (float)pR.red, fG = (float)pC.green, fB = (float)pB.blue, fA = (float)pC.alpha;

		if (gi->rgb_amt > 0.0f) {
			float amt = gi->rgb_amt;
			if (gi->rgb_mode == 0) {
				int xMod = x % 3;
				if (xMod == 0) { fG *= (1.0f - 0.5f*amt); fB *= (1.0f - 0.5f*amt); }
				else if (xMod == 1) { fR *= (1.0f - 0.5f*amt); fB *= (1.0f - 0.5f*amt); }
				else { fR *= (1.0f - 0.5f*amt); fG *= (1.0f - 0.5f*amt); }
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

		__m128 rgba = simd_load_pixel8(fR, fG, fB, fA);
		rgba = simd_mul_channels(rgba, gi->color_temp_r, 1.0f, gi->color_temp_b, 1.0f);

		if (gi->phosphor_mode > 0) {
			if (gi->phosphor_mode == 1) {
				int mx = x % 3;
				if (mx == 0) rgba = simd_mul_channels(rgba, 1.0f, 1.0f - gi->phosphor_intensity, 1.0f - gi->phosphor_intensity, 1.0f);
				else if (mx == 1) rgba = simd_mul_channels(rgba, 1.0f - gi->phosphor_intensity, 1.0f, 1.0f - gi->phosphor_intensity, 1.0f);
				else rgba = simd_mul_channels(rgba, 1.0f - gi->phosphor_intensity, 1.0f - gi->phosphor_intensity, 1.0f, 1.0f);
			} else {
				if ((x + y) % 2 == 0) rgba = simd_mul_scalar(rgba, 1.0f - gi->phosphor_intensity*0.5f);
			}
		}

		if (gi->scanline_op > 0.0f) {
			float freq = gi->scanline_freq < 1.0f ? 1.0f : gi->scanline_freq;
			float dx = x - gi->anchor_x, dy = y - gi->anchor_y;
			float rot_y = dx * gi->scan_sin + dy * gi->scan_cos + gi->anchor_y;
			float val = rot_y + (gi->scanline_phase / 360.0f) * freq;
			float phase = fmodf(val, freq) / freq;
			if (phase < 0.0f) phase += 1.0f;
			float hard = (phase < 0.5f) ? 1.0f : 0.0f;
			float soft = fast_cos(phase * 6.28318f) * 0.5f + 0.5f;
			float pattern = hard * (1.0f - gi->scanline_soft) + soft * gi->scanline_soft;
			rgba = simd_mul_scalar(rgba, 1.0f - gi->scanline_op * (1.0f - pattern));
		}

		if (gi->hum_intensity > 0.0f) {
			float hum_y = y * 0.01f / gi->hum_width + gi->time * gi->hum_speed;
			rgba = simd_mul_scalar(rgba, 1.0f - gi->hum_intensity * (fast_sin(hum_y) * 0.5f + 0.5f));
		}

		if (gi->flicker_amount > 0.0f) rgba = simd_mul_scalar(rgba, gi->flicker_mult);

		if (gi->vignette > 0.0f) {
			float vig = clampf(1.0f - gi->vignette * r2 * 1.5f, 0.0f, 1.0f);
			rgba = simd_mul_scalar(rgba, vig);
		}

		if (gi->noise_amount > 0.0f) {
			unsigned int h = simd_hash((unsigned int)x, (unsigned int)y, gi->frame_count);
			float noise = ((float)(h & 0xFFFF) / 65535.0f) - 0.5f;
			rgba = simd_mul_scalar(rgba, 1.0f + noise * gi->noise_amount);
		}

		rgba = simd_clamp(rgba, (float)PF_MAX_CHAN16);
		fR = simd_extract_r(rgba); fG = simd_extract_g(rgba); fB = simd_extract_b(rgba);
		
		if (gi->bloom_amt > 0.0f) {
			float bR = 0.0f, bG = 0.0f, bB = 0.0f, wSum = 0.0f;
			int radius = gi->bloom_hq ? 3 : 1;
			float spread = gi->bloom_hq ? 2.5f : 1.5f;
			for (int bdy = -radius; bdy <= radius; bdy++) {
				for (int bdx = -radius; bdx <= radius; bdx++) {
					float w = expf(-(float)(bdx*bdx + bdy*bdy) / (2.0f * spread * spread));
					PF_Pixel16 bp = sample16(gi, srcX + (float)bdx * 2.0f, srcY + (float)bdy * 2.0f);
					bR += (float)bp.red * w; bG += (float)bp.green * w; bB += (float)bp.blue * w;
					wSum += w;
				}
			}
			float scale = gi->bloom_amt * 0.5f / wSum;
			fR += bR * scale; fG += bG * scale; fB += bB * scale;
		}

		out_row[x].alpha = (A_u_short)fA;
		out_row[x].red   = (A_u_short)clampf(fR, 0.0f, (float)PF_MAX_CHAN16);
		out_row[x].green = (A_u_short)clampf(fG, 0.0f, (float)PF_MAX_CHAN16);
		out_row[x].blue  = (A_u_short)clampf(fB, 0.0f, (float)PF_MAX_CHAN16);
	}
}

static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
		"%s v%d.%d\r%s", "BlazeCRT", MAJOR_VERSION, MINOR_VERSION, "Hyper-Optimized CRT Engine by Blaze");
	return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
	out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_PIX_INDEPENDENT | PF_OutFlag_NON_PARAM_VARY;
	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
	simd_init_sin_lut();
	return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
	PF_Err err = PF_Err_NONE; PF_ParamDef def;

	/* TUBE */
	AEFX_CLR_STRUCT(def); PF_ADD_TOPIC("Tube", TUBE_START_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Curvature Amount", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, CURVATURE_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_POPUP("Phosphor Mask", 3, 1, "Off|Aperture Grille|Shadow Mask", PHOSPHOR_MODE_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Phosphor Intensity", 0, 100, 0, 100, 50, PF_Precision_HUNDREDTHS, 0, 0, PHOSPHOR_INTENSITY_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_END_TOPIC(TUBE_END_DISK_ID);

	/* SIGNAL */
	AEFX_CLR_STRUCT(def); PF_ADD_TOPIC("Signal", SIGNAL_START_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Scanline Opacity", 0, 100, 0, 100, 50, PF_Precision_HUNDREDTHS, 0, 0, SCANLINE_OPACITY_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Scanline Frequency", 1, 8, 1, 8, 3, PF_Precision_HUNDREDTHS, 0, 0, SCANLINE_FREQ_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Scanline Softness", 0, 100, 0, 100, 20, PF_Precision_HUNDREDTHS, 0, 0, SCANLINE_SOFT_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Scanline Phase", 0, SCANLINE_PHASE_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_ANGLE("Scanline Rotation", 0, SCANLINE_ROTATION_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_POINT("Scanline Anchor", 50, 50, 0, SCANLINE_ANCHOR_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Flicker Amount", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, FLICKER_AMOUNT_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Flicker Speed", 0, 10, 0, 10, 1, PF_Precision_HUNDREDTHS, 0, 0, FLICKER_SPEED_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Hum Bar Intensity", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, HUM_INTENSITY_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Hum Bar Speed", 0, 10, 0, 10, 1, PF_Precision_HUNDREDTHS, 0, 0, HUM_SPEED_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Hum Bar Width", 1, 50, 1, 50, 10, PF_Precision_HUNDREDTHS, 0, 0, HUM_WIDTH_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Interlace Jitter", "On", 0, 0, INTERLACE_ON_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Interlace Shift", 0, 4, 0, 4, 1, PF_Precision_HUNDREDTHS, 0, 0, INTERLACE_SHIFT_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Signal Noise", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, NOISE_AMOUNT_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_END_TOPIC(SIGNAL_END_DISK_ID);

	/* POST */
	AEFX_CLR_STRUCT(def); PF_ADD_TOPIC("Post", POST_START_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("RGB Split Amount", 0, 100, 0, 100, 50, PF_Precision_HUNDREDTHS, 0, 0, RGB_AMOUNT_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_POPUP("RGB Split Mode", 3, 1, "Subpixel|Horizontal|Radial", RGB_MODE_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Chromatic Aberration", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, CHROM_ABB_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Phosphor Bloom", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, BLOOM_AMOUNT_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("High Quality Bloom", "On", 0, 0, BLOOM_HQ_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Color Temp", -100, 100, -100, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, COLOR_TEMP_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Vignette Amount", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, VIGNETTE_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Phosphor Decay", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, DECAY_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Film Grain", 0, 100, 0, 100, 0, PF_Precision_HUNDREDTHS, 0, 0, GRAIN_AMOUNT_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_ADD_SLIDER("Grain Size", 1, 8, 1, 8, 1, GRAIN_SIZE_DISK_ID);
	AEFX_CLR_STRUCT(def); PF_END_TOPIC(POST_END_DISK_ID);

	out_data->num_params = BLAZECRT_NUM_PARAMS; return err;
}

static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
	PF_Err err = PF_Err_NONE; CRTInfo gi; AEFX_CLR_STRUCT(gi);

	gi.curvature = (float)(params[BLAZECRT_CURVATURE]->u.fs_d.value / 100.0);
	gi.phosphor_mode = params[BLAZECRT_PHOSPHOR_MODE]->u.pd.value - 1;
	gi.phosphor_intensity = (float)(params[BLAZECRT_PHOSPHOR_INTENSITY]->u.fs_d.value / 100.0);
	gi.scanline_op = (float)(params[BLAZECRT_SCANLINE_OPACITY]->u.fs_d.value / 100.0);
	gi.scanline_freq = (float)(params[BLAZECRT_SCANLINE_FREQ]->u.fs_d.value);
	gi.scanline_soft = (float)(params[BLAZECRT_SCANLINE_SOFT]->u.fs_d.value / 100.0);
	gi.scanline_phase = (float)params[BLAZECRT_SCANLINE_PHASE]->u.ad.value / 65536.0f;
	gi.scanline_rotation = (float)params[BLAZECRT_SCANLINE_ROTATION]->u.ad.value / 65536.0f;
	gi.anchor_x = (float)params[BLAZECRT_SCANLINE_ANCHOR]->u.td.x_value / 65536.0f;
	gi.anchor_y = (float)params[BLAZECRT_SCANLINE_ANCHOR]->u.td.y_value / 65536.0f;
	gi.flicker_amount = (float)(params[BLAZECRT_FLICKER_AMOUNT]->u.fs_d.value / 100.0);
	gi.flicker_speed = (float)(params[BLAZECRT_FLICKER_SPEED]->u.fs_d.value);
	gi.hum_intensity = (float)(params[BLAZECRT_HUM_INTENSITY]->u.fs_d.value / 100.0);
	gi.hum_speed = (float)(params[BLAZECRT_HUM_SPEED]->u.fs_d.value);
	gi.hum_width = (float)(params[BLAZECRT_HUM_WIDTH]->u.fs_d.value);
	gi.interlace_on = params[BLAZECRT_INTERLACE_ON]->u.bd.value;
	gi.interlace_shift = (float)(params[BLAZECRT_INTERLACE_SHIFT]->u.fs_d.value);
	gi.noise_amount = (float)(params[BLAZECRT_NOISE_AMOUNT]->u.fs_d.value / 100.0);
	gi.rgb_amt = (float)(params[BLAZECRT_RGB_AMOUNT]->u.fs_d.value / 100.0);
	gi.rgb_mode = params[BLAZECRT_RGB_MODE]->u.pd.value - 1;
	gi.chrom_abb = (float)(params[BLAZECRT_CHROM_ABB]->u.fs_d.value / 100.0);
	gi.bloom_amt = (float)(params[BLAZECRT_BLOOM_AMOUNT]->u.fs_d.value / 100.0);
	gi.bloom_hq = params[BLAZECRT_BLOOM_HQ]->u.bd.value;
	gi.color_temp = (float)(params[BLAZECRT_COLOR_TEMP]->u.fs_d.value / 100.0);
	gi.vignette = (float)(params[BLAZECRT_VIGNETTE]->u.fs_d.value / 100.0);
	gi.decay = (float)(params[BLAZECRT_DECAY]->u.fs_d.value / 100.0);
	gi.grain_amt = (float)(params[BLAZECRT_GRAIN_AMOUNT]->u.fs_d.value / 100.0);
	gi.grain_size = params[BLAZECRT_GRAIN_SIZE]->u.sd.value;

	gi.width = output->width; gi.height = output->height;
	gi.rowbytes_in = params[BLAZECRT_INPUT]->u.ld.rowbytes; gi.rowbytes_out = output->rowbytes;
	gi.time = (float)((double)in_data->current_time / (double)in_data->time_scale);
	gi.frame_count = (unsigned int)(in_data->current_time / in_data->time_step);
	gi.in_data_ptr = params[BLAZECRT_INPUT]->u.ld.data; gi.out_data_ptr = output->data;

	gi.scan_sin = fast_sin(gi.scanline_rotation * 0.0174533f); gi.scan_cos = fast_cos(gi.scanline_rotation * 0.0174533f);
	float flick_phase = gi.time * gi.flicker_speed * 10.0f;
	gi.flicker_mult = 1.0f - gi.flicker_amount * (fast_sin(flick_phase)*0.5f+0.5f);
	
	if (gi.color_temp > 0.0f) { gi.color_temp_r = 1.0f + gi.color_temp * 0.2f; gi.color_temp_b = 1.0f - gi.color_temp * 0.2f; }
	else { gi.color_temp_r = 1.0f + gi.color_temp * 0.2f; gi.color_temp_b = 1.0f - gi.color_temp * 0.2f; }

	int is_16 = PF_WORLD_IS_DEEP(output), h = gi.height;
	#pragma omp parallel for schedule(dynamic, 8)
	for (int y = 0; y < h; y++) { if (is_16) ProcessRow16(&gi, y); else ProcessRow8(&gi, y); }
	return err;
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr, SPBasicSuite* inSPBasicSuitePtr, const char* inHostName, const char* inHostVersion) {
	PF_Err result = PF_Err_NONE;
	result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, "BlazeCRT", "ADBE BlazeCRT", "Blaze Plugins", AE_RESERVED_INFO, "EffectMain", "https://github.com/blazethetechguy/BlazeCRT");
	return result;
}

PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT: err = About(in_data, out_data, params, output); break;
			case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_RENDER: err = Render(in_data, out_data, params, output); break;
		}
	} catch (PF_Err& thrown_err) { err = thrown_err; }
	return err;
}
