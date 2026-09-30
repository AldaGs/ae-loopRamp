/*	LoopRamp.cpp

	Loop Ramp - a multi-stop Gradient Ramp whose colors can be offset in a loop.

	    pixel (x,y) -> t along Start->End (linear) or distance from Start (radial)
	    u = frac(t * Repeat - Offset)  -> periodic LUT -> color, blended over input

	The LUT covers ONE period [0,1). Between the last stop and the first stop
	(+1) it keeps interpolating, so Offset can run forever without a seam.
	Put a stop at 0% and another at 100% if you WANT a hard seam (sawtooth).

	Revision History
	Version		Change											Engineer	Date
	=======		======											========	======
	1.0			Cloned from GradientMap						aldai		9/30/2026
*/

#include "LoopRamp.h"
#include <math.h>

/* =========================================================================
   Boilerplate command handlers.
   ========================================================================= */

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
		STR(StrID_Name),
		MAJOR_VERSION,
		MINOR_VERSION,
		STR(StrID_Description));

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

	// CUSTOM_UI: we draw + edit the gradient bar. Must match PiPL OutFlags.
	out_data->out_flags  =  PF_OutFlag_DEEP_COLOR_AWARE |
							PF_OutFlag_USE_OUTPUT_EXTENT |
							PF_OutFlag_CUSTOM_UI;

	// Must match PiPL OutFlags_2.
	out_data->out_flags2 =  PF_OutFlag2_SUPPORTS_SMART_RENDER |
							PF_OutFlag2_FLOAT_COLOR_AWARE |
							PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

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

	// Gradient editor bar. Width is a minimum; the draw uses the panel width.
	AEFX_CLR_STRUCT(def);
	ERR(LR_NewDefaultArb(in_data, &def.u.arb_d.dephault));
	if (err) return err;
	PF_ADD_ARBITRARY2(STR(StrID_Gradient_Param_Name), 200, 52,
					  PF_ParamFlag_NONE,
					  PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL,
					  def.u.arb_d.dephault, GRADIENT_DISK_ID, LR_ARB_REFCON);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POINT(STR(StrID_Start_Param_Name), 0, 50, FALSE, START_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POINT(STR(StrID_End_Param_Name), 100, 50, FALSE, END_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP(STR(StrID_Shape_Param_Name), 2, LR_SHAPE_LINEAR,
				 STR(StrID_Shape_Choices), SHAPE_DISK_ID);

	// Offset in % of one cycle: 100 = colors travel one full loop. Huge valid
	// range so it can be keyframed/expression-driven (time*100) indefinitely.
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(STR(StrID_Offset_Param_Name),
						 -1000000, 1000000, -100, 100, 0,
						 PF_Precision_TENTHS, 0, 0, OFFSET_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(STR(StrID_Repeat_Param_Name),
						 0.01, 100, 1, 10, 1,
						 PF_Precision_HUNDREDTHS, 0, 0, REPEAT_DISK_ID);

	// Ramp Scatter, like AE's Gradient Ramp: jitters each pixel's position on
	// the ramp with static noise so smooth blends don't band.
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(STR(StrID_Scatter_Param_Name), 0, 1000, 0, 100, 0,
						 PF_Precision_TENTHS, 0, 0, SCATTER_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP(STR(StrID_Interp_Param_Name), 3, LR_INTERP_LINEAR,
				 STR(StrID_Interp_Choices), INTERP_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(STR(StrID_Blend_Param_Name), 0, 100, 0, 100, 0,
						 PF_Precision_HUNDREDTHS, 0, 0, BLEND_DISK_ID);

	// Events only in the Effect Controls panel (the bar); no comp overlay.
	PF_CustomUIInfo ci;
	AEFX_CLR_STRUCT(ci);
	ci.events = PF_CustomEFlag_EFFECT;
	ERR((*(in_data->inter.register_ui))(in_data->effect_ref, &ci));

	out_data->num_params = LR_NUM_PARAMS;

	return err;
}

/* =========================================================================
   Periodic LUT. Stops are sorted by position; stop index j outside [0,n) wraps
   to j mod n, one period (+/-1) along, so the segment after the last stop
   blends into the first.
   ========================================================================= */

static PF_FpLong
CatmullRom (PF_FpLong p0, PF_FpLong p1, PF_FpLong p2, PF_FpLong p3, PF_FpLong f)
{
	PF_FpLong f2 = f * f, f3 = f2 * f;
	return 0.5 * ((2.0 * p1)
				+ (-p0 + p2) * f
				+ (2.0*p0 - 5.0*p1 + 4.0*p2 - p3) * f2
				+ (-p0 + 3.0*p1 - 3.0*p2 + p3) * f3);
}

void
LR_BuildLUT (float lut[][3], const LRArb *arb, A_long interp)
{
	int n = arb->count;
	if (n < 1) {							// never expected; paint black
		for (int i = 0; i < LR_LUT_SIZE; ++i) lut[i][0] = lut[i][1] = lut[i][2] = 0.0f;
		return;
	}
	if (n > LR_ARB_CAP) n = LR_ARB_CAP;

	// Stops are stored in UI order; sort a copy by position.
	static thread_local PF_FpLong pos[LR_ARB_CAP], col[LR_ARB_CAP][3];
	for (int i = 0; i < n; ++i) {
		const LRStop &s = arb->stops[i];
		pos[i] = (s.pos < 0.0f) ? 0.0 : (s.pos > 1.0f) ? 1.0 : s.pos;
		col[i][0] = s.r;  col[i][1] = s.g;  col[i][2] = s.b;
	}
	for (int i = 1; i < n; ++i) {			// insertion sort: n is small
		for (int j = i; j > 0 && pos[j - 1] > pos[j]; --j) {
			PF_FpLong tp = pos[j]; pos[j] = pos[j - 1]; pos[j - 1] = tp;
			for (int c = 0; c < 3; ++c) {
				PF_FpLong tc = col[j][c]; col[j][c] = col[j - 1][c]; col[j - 1][c] = tc;
			}
		}
	}

	#define WRAP(j)		((((j) % n) + n) % n)
	#define P(j)		(pos[WRAP(j)] + floor((PF_FpLong)(j) / n))

	for (int i = 0; i < LR_LUT_SIZE; ++i) {
		PF_FpLong t = (PF_FpLong)i / LR_LUT_SIZE;		// [0,1), periodic

		int k = -1;										// P(-1) <= 0 <= t always
		while (k < n - 1 && P(k + 1) <= t) ++k;			// P(n) >= 1 > t always
		PF_FpLong span = P(k + 1) - P(k);
		PF_FpLong f    = (span <= 0.0) ? 0.0 : (t - P(k)) / span;

		for (int c = 0; c < 3; ++c) {
			PF_FpLong v;
			if (interp == LR_INTERP_CONSTANT) {
				v = col[WRAP(k)][c];
			} else if (interp == LR_INTERP_SMOOTH) {
				v = CatmullRom(col[WRAP(k - 1)][c], col[WRAP(k)][c],
							   col[WRAP(k + 1)][c], col[WRAP(k + 2)][c], f);
				if (v < 0.0) v = 0.0; else if (v > 1.0) v = 1.0;
			} else {
				v = (1.0 - f) * col[WRAP(k)][c] + f * col[WRAP(k + 1)][c];
			}
			lut[i][c] = (float)v;
		}
	}

	#undef P
	#undef WRAP
}

/* Snapshot every param into LRInfo. p[] is indexed like params[] (p[0] unused)
   so the classic and SmartFX paths share it. originX/Y = layer pixel of the
   output world's (0,0). */
static void
LR_ReadParams (LRInfo *info, PF_InData *in_data, PF_ParamDef *p[],
			   PF_FpLong originX, PF_FpLong originY)
{
	// All ramp math happens in FULL-RES layer pixels (where point params live);
	// LR_Shade maps each rendered pixel back up. Scaling the points down
	// instead would skew angles/circles when x and y downsample differ, and
	// would change the scatter grain with the resolution.
	info->invDsx = (PF_FpLong)in_data->downsample_x.den / in_data->downsample_x.num;
	info->invDsy = (PF_FpLong)in_data->downsample_y.den / in_data->downsample_y.num;

	info->sx = FIX_2_FLOAT(p[LR_START]->u.td.x_value);
	info->sy = FIX_2_FLOAT(p[LR_START]->u.td.y_value);
	info->dx = FIX_2_FLOAT(p[LR_END]->u.td.x_value) - info->sx;
	info->dy = FIX_2_FLOAT(p[LR_END]->u.td.y_value) - info->sy;

	PF_FpLong len2 = info->dx * info->dx + info->dy * info->dy;
	info->invLen2 = (len2 > 0.0) ? 1.0 / len2 : 0.0;
	info->invLen  = (len2 > 0.0) ? 1.0 / sqrt(len2) : 0.0;

	info->originX = originX;
	info->originY = originY;
	info->shape   = p[LR_SHAPE]->u.pd.value;
	info->offset  = p[LR_OFFSET]->u.fs_d.value / 100.0;
	info->repeat  = p[LR_REPEAT]->u.fs_d.value;
	info->blendF  = p[LR_BLEND]->u.fs_d.value / 100.0;
	// 100 % scatter = +/- 5 % of one cycle of jitter.
	info->scatter = p[LR_SCATTER]->u.fs_d.value / 100.0 * 0.1;

	PF_Handle arbH = p[LR_GRADIENT]->u.arb_d.value;
	const LRArb *arb = arbH ? reinterpret_cast<const LRArb*>(PF_LOCK_HANDLE(arbH)) : NULL;
	if (arb) {
		LR_BuildLUT(info->lut, arb, p[LR_INTERP]->u.pd.value);
		PF_UNLOCK_HANDLE(arbH);
	} else {
		LRArb empty;
		empty.count = 0;
		LR_BuildLUT(info->lut, &empty, LR_INTERP_LINEAR);
	}
}

// Stable per-pixel noise in [0,1) (integer hash; same grain every frame).
static inline PF_FpLong
Hash01 (A_long x, A_long y)
{
	A_u_long h = (A_u_long)x * 374761393u + (A_u_long)y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (h & 0xFFFFFF) / 16777216.0;
}

// Ramp color at world pixel (xL,yL), blended with the source rgb (all [0,1]).
static void
LR_Shade (const LRInfo *info, A_long xL, A_long yL,
		  PF_FpLong sr, PF_FpLong sg, PF_FpLong sb,
		  PF_FpLong *orP, PF_FpLong *ogP, PF_FpLong *obP)
{
	// Center of this (possibly downsampled) pixel, in full-res layer pixels.
	// Identity at full resolution.
	PF_FpLong fx = (xL + info->originX + 0.5) * info->invDsx - 0.5;
	PF_FpLong fy = (yL + info->originY + 0.5) * info->invDsy - 0.5;
	PF_FpLong px = fx - info->sx;
	PF_FpLong py = fy - info->sy;

	PF_FpLong t = (info->shape == LR_SHAPE_RADIAL)
		? sqrt(px * px + py * py) * info->invLen
		: (px * info->dx + py * info->dy) * info->invLen2;

	PF_FpLong u = t * info->repeat - info->offset;
	if (info->scatter > 0.0) {
		// Keyed on the full-res pixel, so lower resolutions show the same
		// grain, just sampled more sparsely.
		u += (Hash01((A_long)floor(fx + 0.5), (A_long)floor(fy + 0.5)) - 0.5)
			 * info->scatter;
	}
	u -= floor(u);										// wrap into [0,1)

	PF_FpLong posF = u * LR_LUT_SIZE;
	int   i0   = (int)posF;
	if (i0 >= LR_LUT_SIZE) i0 = LR_LUT_SIZE - 1;		// u rounding to 1.0
	int   i1   = (i0 + 1) % LR_LUT_SIZE;				// periodic neighbor
	PF_FpLong frac = posF - i0;

	PF_FpLong a  = info->blendF;						// 0 = full ramp
	PF_FpLong mr = (1.0 - frac) * info->lut[i0][0] + frac * info->lut[i1][0];
	PF_FpLong mg = (1.0 - frac) * info->lut[i0][1] + frac * info->lut[i1][1];
	PF_FpLong mb = (1.0 - frac) * info->lut[i0][2] + frac * info->lut[i1][2];
	*orP = (1.0 - a) * mr + a * sr;
	*ogP = (1.0 - a) * mg + a * sg;
	*obP = (1.0 - a) * mb + a * sb;
}

/* =========================================================================
   Per-pixel functions (8 / 16 / 32-bit). Alpha passes through, so the ramp
   fills the layer's own shape (a solid, a text layer, ...).
   ========================================================================= */

static PF_Err
LRFunc8 (void *refcon, A_long xL, A_long yL, PF_Pixel8 *inP, PF_Pixel8 *outP)
{
	PF_FpLong r, g, b;
	LR_Shade(reinterpret_cast<LRInfo*>(refcon), xL, yL,
			 inP->red / 255.0, inP->green / 255.0, inP->blue / 255.0, &r, &g, &b);
	outP->alpha = inP->alpha;
	outP->red   = (A_u_char) MIN(MAX(r * 255.0 + 0.5, 0), PF_MAX_CHAN8);
	outP->green = (A_u_char) MIN(MAX(g * 255.0 + 0.5, 0), PF_MAX_CHAN8);
	outP->blue  = (A_u_char) MIN(MAX(b * 255.0 + 0.5, 0), PF_MAX_CHAN8);
	return PF_Err_NONE;
}

static PF_Err
LRFunc16 (void *refcon, A_long xL, A_long yL, PF_Pixel16 *inP, PF_Pixel16 *outP)
{
	PF_FpLong r, g, b;
	LR_Shade(reinterpret_cast<LRInfo*>(refcon), xL, yL,
			 (PF_FpLong)inP->red / PF_MAX_CHAN16,
			 (PF_FpLong)inP->green / PF_MAX_CHAN16,
			 (PF_FpLong)inP->blue / PF_MAX_CHAN16, &r, &g, &b);
	outP->alpha = inP->alpha;
	outP->red   = (A_u_short) MIN(MAX(r * PF_MAX_CHAN16 + 0.5, 0), PF_MAX_CHAN16);
	outP->green = (A_u_short) MIN(MAX(g * PF_MAX_CHAN16 + 0.5, 0), PF_MAX_CHAN16);
	outP->blue  = (A_u_short) MIN(MAX(b * PF_MAX_CHAN16 + 0.5, 0), PF_MAX_CHAN16);
	return PF_Err_NONE;
}

static PF_Err
LRFuncFloat (void *refcon, A_long xL, A_long yL, PF_PixelFloat *inP, PF_PixelFloat *outP)
{
	PF_FpLong r, g, b;
	LR_Shade(reinterpret_cast<LRInfo*>(refcon), xL, yL,
			 inP->red, inP->green, inP->blue, &r, &g, &b);
	outP->alpha = inP->alpha;
	outP->red   = (float)r;
	outP->green = (float)g;
	outP->blue  = (float)b;
	return PF_Err_NONE;
}

/* =========================================================================
   Classic-render path (Premiere / legacy fallback).
   ========================================================================= */

static PF_Err
Render (
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	PF_Err				err		= PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);

	LRInfo *infoP = new LRInfo;		// ~12 KB LUT: keep it off the stack
	LR_ReadParams(infoP, in_data, params, 0, 0);

	A_long linesL = output->extent_hint.bottom - output->extent_hint.top;

	if (PF_WORLD_IS_DEEP(output)) {
		ERR(suites.Iterate16Suite2()->iterate(	in_data, 0, linesL,
												&params[LR_INPUT]->u.ld,
												NULL, (void*)infoP, LRFunc16, output));
	} else {
		ERR(suites.Iterate8Suite2()->iterate(	in_data, 0, linesL,
												&params[LR_INPUT]->u.ld,
												NULL, (void*)infoP, LRFunc8, output));
	}

	delete infoP;
	return err;
}

/* =========================================================================
   SmartFX path (8/16/32-bit).
   ========================================================================= */

static PF_Err
PreRender (
	PF_InData			*in_data,
	PF_OutData			*out_data,
	PF_PreRenderExtra	*extra)
{
	PF_Err				err = PF_Err_NONE, err2 = PF_Err_NONE;
	PF_RenderRequest	req = extra->input->output_request;
	PF_CheckoutResult	in_result;
	PF_ParamDef			defs[LR_NUM_PARAMS];
	PF_ParamDef			*p[LR_NUM_PARAMS];

	AEGP_SuiteHandler	suites(in_data->pica_basicP);

	PF_Handle infoH = suites.HandleSuite1()->host_new_handle(sizeof(LRInfo));
	if (!infoH) return PF_Err_OUT_OF_MEMORY;

	LRInfo *infoP = reinterpret_cast<LRInfo*>(suites.HandleSuite1()->host_lock_handle(infoH));
	if (!infoP) { suites.HandleSuite1()->host_dispose_handle(infoH); return PF_Err_OUT_OF_MEMORY; }

	extra->output->pre_render_data = infoH;
	AEFX_CLR_STRUCT(*infoP);

	// Check out every param; remember how many succeeded so exactly those get
	// checked back in.
	p[0] = NULL;
	A_long checkedOut = 1;
	while (checkedOut < LR_NUM_PARAMS && !err) {
		AEFX_CLR_STRUCT(defs[checkedOut]);
		p[checkedOut] = &defs[checkedOut];
		err = PF_CHECKOUT_PARAM(in_data, checkedOut, in_data->current_time,
								in_data->time_step, in_data->time_scale, &defs[checkedOut]);
		if (!err) ++checkedOut;
	}

	req.preserve_rgb_of_zero_alpha = TRUE;
	req.field = PF_Field_FRAME;

	ERR(extra->cb->checkout_layer(	in_data->effect_ref,
									LR_INPUT, LR_INPUT, &req,
									in_data->current_time, in_data->time_step, in_data->time_scale,
									&in_result));

	if (!err) {
		LR_ReadParams(infoP, in_data, p,
					  in_result.result_rect.left, in_result.result_rect.top);

		extra->output->result_rect     = in_result.result_rect;
		extra->output->max_result_rect = in_result.max_result_rect;
	}

	for (A_long i = 1; i < checkedOut; ++i) {
		ERR2(PF_CHECKIN_PARAM(in_data, &defs[i]));
	}

	suites.HandleSuite1()->host_unlock_handle(infoH);
	return err;
}

static PF_Err
SmartRender (
	PF_InData			*in_data,
	PF_OutData			*out_data,
	PF_SmartRenderExtra	*extra)
{
	PF_Err				err = PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);
	PF_EffectWorld		*inputP = NULL, *outputP = NULL;

	LRInfo *infoP = reinterpret_cast<LRInfo*>(
		suites.HandleSuite1()->host_lock_handle(reinterpret_cast<PF_Handle>(extra->input->pre_render_data)));
	if (!infoP) return PF_Err_BAD_CALLBACK_PARAM;

	ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, LR_INPUT, &inputP));
	ERR(extra->cb->checkout_output(in_data->effect_ref, &outputP));

	if (!err && inputP && outputP) {
		A_long linesL = outputP->height;

		switch (extra->input->bitdepth) {
			case 32:
				ERR(suites.IterateFloatSuite2()->iterate(in_data, 0, linesL, inputP,
						&outputP->extent_hint, (void*)infoP, LRFuncFloat, outputP));
				break;
			case 16:
				ERR(suites.Iterate16Suite2()->iterate(in_data, 0, linesL, inputP,
						&outputP->extent_hint, (void*)infoP, LRFunc16, outputP));
				break;
			case 8:
				ERR(suites.Iterate8Suite2()->iterate(in_data, 0, linesL, inputP,
						&outputP->extent_hint, (void*)infoP, LRFunc8, outputP));
				break;
			default:
				err = PF_Err_BAD_CALLBACK_PARAM;
				break;
		}
	}

	suites.HandleSuite1()->host_unlock_handle(reinterpret_cast<PF_Handle>(extra->input->pre_render_data));
	return err;
}

/* =========================================================================
   Registration + entry point.
   ========================================================================= */

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
		"Loop Ramp",				// Name
		"aldai LoopRamp",			// Match Name
		"Learning",					// Category
		AE_RESERVED_INFO,
		"EffectMain",
		"https://www.adobe.com");

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
			case PF_Cmd_EVENT:
				err = LR_HandleEvent(in_data, out_data, params, output, reinterpret_cast<PF_EventExtra*>(extra));
				break;
			case PF_Cmd_ARBITRARY_CALLBACK:
				err = LR_HandleArbitrary(in_data, out_data, reinterpret_cast<PF_ArbParamsExtra*>(extra));
				break;
			case PF_Cmd_SMART_PRE_RENDER:
				err = PreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra));
				break;
			case PF_Cmd_SMART_RENDER:
				err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra));
				break;
		}
	}
	catch(PF_Err &thrown_err){
		err = thrown_err;
	}
	return err;
}
