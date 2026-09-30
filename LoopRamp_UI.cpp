/*	LoopRamp_UI.cpp

	The Gradient param: an arbitrary-data stop list (LRArb) plus the bar that
	draws and edits it in the Effect Controls panel. Modeled on the SDK's
	UI/ColorGrid sample.

	Editing:
	    click the bar            add a stop (takes the color under the cursor), keep dragging to place it
	    drag a stop marker       move it
	    double-click a marker    pick its color
	    Alt+click a marker, or drag it well below the bar    delete it (min 2 stops)
*/

#include "LoopRamp.h"
#include <string.h>
#include <math.h>

/* =========================================================================
   Arbitrary data callbacks: how AE creates, copies, saves and keyframes it.
   ========================================================================= */

static PF_Handle
NewArbHandle (PF_InData *in_data)
{
	PF_Handle h = PF_NEW_HANDLE(sizeof(LRArb));
	if (h) {
		LRArb *a = reinterpret_cast<LRArb*>(PF_LOCK_HANDLE(h));
		memset(a, 0, sizeof(LRArb));
		a->magic = LR_ARB_MAGIC;
		PF_UNLOCK_HANDLE(h);
	}
	return h;
}

// Four evenly spaced stops, so the default ramp already loops seamlessly.
PF_Err
LR_NewDefaultArb (PF_InData *in_data, PF_ArbitraryH *arbPH)
{
	PF_Handle h = NewArbHandle(in_data);
	if (!h) return PF_Err_OUT_OF_MEMORY;

	static const LRStop kDefault[4] = {
		{ 0.00f, 1.00f, 0.24f, 0.35f },
		{ 0.25f, 1.00f, 0.78f, 0.24f },
		{ 0.50f, 0.16f, 0.86f, 0.78f },
		{ 0.75f, 0.27f, 0.35f, 1.00f },
	};
	LRArb *a = reinterpret_cast<LRArb*>(PF_LOCK_HANDLE(h));
	a->count = 4;
	memcpy(a->stops, kDefault, sizeof(kDefault));
	PF_UNLOCK_HANDLE(h);

	*arbPH = h;
	return PF_Err_NONE;
}

PF_Err
LR_HandleArbitrary (PF_InData *in_data, PF_OutData *out_data, PF_ArbParamsExtra *extra)
{
	PF_Err err = PF_Err_NONE;

	switch (extra->which_function) {

	case PF_Arbitrary_NEW_FUNC:
		err = LR_NewDefaultArb(in_data, extra->u.new_func_params.arbPH);
		break;

	case PF_Arbitrary_DISPOSE_FUNC:
		if (extra->u.dispose_func_params.arbH) PF_DISPOSE_HANDLE(extra->u.dispose_func_params.arbH);
		break;

	case PF_Arbitrary_COPY_FUNC: {
		PF_Handle src = extra->u.copy_func_params.src_arbH;
		PF_Handle dst = NewArbHandle(in_data);
		if (!dst) { err = PF_Err_OUT_OF_MEMORY; break; }
		if (src) {
			memcpy(PF_LOCK_HANDLE(dst), PF_LOCK_HANDLE(src), sizeof(LRArb));
			PF_UNLOCK_HANDLE(src);
			PF_UNLOCK_HANDLE(dst);
		}
		*extra->u.copy_func_params.dst_arbPH = dst;
		break;
	}

	// Only `count` stops go to disk (see LR_ARB_FLAT_SIZE).
	case PF_Arbitrary_FLAT_SIZE_FUNC: {
		LRArb *a = reinterpret_cast<LRArb*>(PF_LOCK_HANDLE(extra->u.flat_size_func_params.arbH));
		*extra->u.flat_size_func_params.flat_data_sizePLu = (A_u_long)LR_ARB_FLAT_SIZE(a->count);
		PF_UNLOCK_HANDLE(extra->u.flat_size_func_params.arbH);
		break;
	}

	case PF_Arbitrary_FLATTEN_FUNC: {
		LRArb *a = reinterpret_cast<LRArb*>(PF_LOCK_HANDLE(extra->u.flatten_func_params.arbH));
		size_t n = LR_ARB_FLAT_SIZE(a->count);
		if (extra->u.flatten_func_params.buf_sizeLu >= n) {
			memcpy(extra->u.flatten_func_params.flat_dataPV, a, n);
		} else {
			err = PF_Err_INTERNAL_STRUCT_DAMAGED;
		}
		PF_UNLOCK_HANDLE(extra->u.flatten_func_params.arbH);
		break;
	}

	case PF_Arbitrary_UNFLATTEN_FUNC: {
		// Validate everything read from disk before trusting it.
		const LRArb *src = reinterpret_cast<const LRArb*>(extra->u.unflatten_func_params.flat_dataPV);
		A_u_long     sz  = extra->u.unflatten_func_params.buf_sizeLu;
		if (sz < LR_ARB_FLAT_SIZE(0) || src->magic != LR_ARB_MAGIC ||
			src->count < 1 || src->count > LR_ARB_CAP || sz < LR_ARB_FLAT_SIZE(src->count)) {
			err = PF_Err_INTERNAL_STRUCT_DAMAGED;
			break;
		}
		PF_Handle h = NewArbHandle(in_data);
		if (!h) { err = PF_Err_OUT_OF_MEMORY; break; }
		memcpy(PF_LOCK_HANDLE(h), src, LR_ARB_FLAT_SIZE(src->count));
		PF_UNLOCK_HANDLE(h);
		*extra->u.unflatten_func_params.arbPH = h;
		break;
	}

	// Keyframes: same stop count -> every stop's position and color tween.
	// Different counts can't be matched up, so it holds the left keyframe.
	case PF_Arbitrary_INTERP_FUNC: {
		PF_Handle lH = extra->u.interp_func_params.left_arbH;
		PF_Handle rH = extra->u.interp_func_params.right_arbH;
		PF_Handle h  = NewArbHandle(in_data);
		if (!h) { err = PF_Err_OUT_OF_MEMORY; break; }
		const LRArb *l = reinterpret_cast<const LRArb*>(PF_LOCK_HANDLE(lH));
		const LRArb *r = reinterpret_cast<const LRArb*>(PF_LOCK_HANDLE(rH));
		LRArb       *o = reinterpret_cast<LRArb*>(PF_LOCK_HANDLE(h));
		memcpy(o, l, sizeof(LRArb));
		if (l->count == r->count) {
			float t = (float)extra->u.interp_func_params.tF;
			for (A_long i = 0; i < l->count; ++i) {
				const LRStop &a = l->stops[i], &b = r->stops[i];
				LRStop &s = o->stops[i];
				s.pos = a.pos + (b.pos - a.pos) * t;
				s.r   = a.r   + (b.r   - a.r)   * t;
				s.g   = a.g   + (b.g   - a.g)   * t;
				s.b   = a.b   + (b.b   - a.b)   * t;
			}
		}
		PF_UNLOCK_HANDLE(h);
		PF_UNLOCK_HANDLE(rH);
		PF_UNLOCK_HANDLE(lH);
		*extra->u.interp_func_params.interpPH = h;
		break;
	}

	case PF_Arbitrary_COMPARE_FUNC: {
		PF_Handle aH = extra->u.compare_func_params.a_arbH;
		PF_Handle bH = extra->u.compare_func_params.b_arbH;
		const LRArb *a = reinterpret_cast<const LRArb*>(PF_LOCK_HANDLE(aH));
		const LRArb *b = reinterpret_cast<const LRArb*>(PF_LOCK_HANDLE(bH));
		int c = (a->count != b->count) ? (a->count - b->count)
									   : memcmp(a, b, LR_ARB_FLAT_SIZE(a->count));
		*extra->u.compare_func_params.compareP =
			(c == 0) ? PF_ArbCompare_EQUAL : (c > 0) ? PF_ArbCompare_MORE : PF_ArbCompare_LESS;
		PF_UNLOCK_HANDLE(bH);
		PF_UNLOCK_HANDLE(aH);
		break;
	}

	// ponytail: no text form, so copying the param as text gives nothing.
	// Add PRINT/SCAN if pasting gradients between apps is ever wanted.
	case PF_Arbitrary_PRINT_SIZE_FUNC:
		*extra->u.print_size_func_params.print_sizePLu = 0;
		break;
	}
	return err;
}

/* =========================================================================
   Editor bar layout. All in Effect Controls panel coordinates.
   ========================================================================= */

#define	BAR_INSET		10
#define	BAR_TOP			4
#define	BAR_H			24
#define	MARK_HALF		5
#define	MARK_H			14
#define	DELETE_DIST		40		// drag this far below the bar to delete

typedef struct { float l, r, t, b; } BarRect;

static BarRect
GetBar (const PF_Rect &frame)
{
	BarRect br;
	br.l = (float)frame.left + BAR_INSET;
	br.r = (float)frame.right - BAR_INSET;
	if (br.r < br.l + 20) br.r = br.l + 20;
	br.t = (float)frame.top + BAR_TOP;
	br.b = br.t + BAR_H;
	return br;
}

static float PosToX (const BarRect &br, float pos) { return br.l + pos * (br.r - br.l); }

static float
XToPos (const BarRect &br, float x)
{
	float p = (x - br.l) / (br.r - br.l);
	return (p < 0.0f) ? 0.0f : (p > 1.0f) ? 1.0f : p;
}

// Index of the marker under (x,y), or -1. Later stops draw on top, so they win.
static A_long
HitStop (const BarRect &br, const LRArb *a, float x, float y)
{
	if (y < br.b || y > br.b + MARK_H + 2) return -1;
	for (A_long i = a->count - 1; i >= 0; --i) {
		if (fabsf(x - PosToX(br, a->stops[i].pos)) <= MARK_HALF + 1) return i;
	}
	return -1;
}

static void
DeleteStop (LRArb *a, A_long i)
{
	memmove(&a->stops[i], &a->stops[i + 1], (a->count - i - 1) * sizeof(LRStop));
	--a->count;
}

/* =========================================================================
   Events.
   ========================================================================= */

static PF_Err
DoClickOrDrag (PF_InData *in_data, PF_ParamDef *params[], PF_EventExtra *extra, PF_Boolean *changedPB)
{
	PF_Err				err	= PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);
	PF_Handle			arbH = params[LR_GRADIENT]->u.arb_d.value;
	LRArb				*a = arbH ? reinterpret_cast<LRArb*>(PF_LOCK_HANDLE(arbH)) : NULL;
	if (!a) return err;

	BarRect br = GetBar(extra->effect_win.current_frame);
	float   mx = (float)extra->u.do_click.screen_point.h;
	float   my = (float)extra->u.do_click.screen_point.v;

	if (extra->e_type == PF_Event_DRAG) {
		A_long i = (A_long)extra->u.do_click.continue_refcon[0] - 1;
		if (i >= 0 && i < a->count) {
			if (extra->u.do_click.last_time && my > br.b + DELETE_DIST && a->count > 2) {
				DeleteStop(a, i);
			} else {
				a->stops[i].pos = XToPos(br, mx);
			}
			*changedPB = TRUE;
		}
		extra->u.do_click.send_drag = !extra->u.do_click.last_time;
		extra->evt_out_flags |= PF_EO_HANDLED_EVENT;

	} else {	// PF_Event_DO_CLICK
		A_long hit = HitStop(br, a, mx, my);

		if (hit >= 0 && (extra->u.do_click.modifiers & PF_Mod_OPT_ALT_KEY)) {
			if (a->count > 2) { DeleteStop(a, hit); *changedPB = TRUE; }
			extra->evt_out_flags |= PF_EO_HANDLED_EVENT;

		} else if (hit >= 0 && extra->u.do_click.num_clicks >= 2) {
			if (in_data->appl_id != kAppID_Premiere) {	// Premiere has no picker
				LRStop &s = a->stops[hit];
				PF_PixelFloat c = { 1.0f, s.r, s.g, s.b }, picked = c;	// alpha, r, g, b
				PF_Err pe = suites.AppSuite4()->PF_AppColorPickerDialog("Stop Color", &c, TRUE, &picked);
				if (!pe) {
					s.r = picked.red;  s.g = picked.green;  s.b = picked.blue;
					*changedPB = TRUE;
				}
			}
			extra->evt_out_flags |= PF_EO_HANDLED_EVENT;

		} else if (hit >= 0) {
			extra->u.do_click.send_drag = TRUE;
			extra->u.do_click.continue_refcon[0] = hit + 1;
			extra->evt_out_flags |= PF_EO_HANDLED_EVENT;

		} else if (my >= br.t && my <= br.b && mx >= br.l && mx <= br.r && a->count < LR_ARB_CAP) {
			// New stop takes the color already there, so the ramp doesn't jump.
			float (*lut)[3] = new float[LR_LUT_SIZE][3];
			LR_BuildLUT(lut, a, params[LR_INTERP]->u.pd.value);
			float  pos = XToPos(br, mx);
			A_long li  = (A_long)(pos * LR_LUT_SIZE) % LR_LUT_SIZE;
			LRStop &s  = a->stops[a->count];
			s.pos = pos;  s.r = lut[li][0];  s.g = lut[li][1];  s.b = lut[li][2];
			delete [] lut;

			extra->u.do_click.send_drag = TRUE;
			extra->u.do_click.continue_refcon[0] = a->count + 1;
			++a->count;
			*changedPB = TRUE;
			extra->evt_out_flags |= PF_EO_HANDLED_EVENT;
		}
	}

	PF_UNLOCK_HANDLE(arbH);
	return err;
}

static PF_Err
DrawBar (PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_EventExtra *extra)
{
	PF_Err				err = PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);
	AEFX_DrawbotSuitesScoper db(in_data, out_data);
	DRAWBOT_Suites		*ds = db.Get();

	DRAWBOT_DrawRef		drawRef = NULL;
	DRAWBOT_SupplierRef	supplier = NULL;
	DRAWBOT_SurfaceRef	surface = NULL;
	ERR(suites.EffectCustomUISuite1()->PF_GetDrawingReference(extra->contextH, &drawRef));
	ERR(ds->drawbot_suiteP->GetSupplier(drawRef, &supplier));
	ERR(ds->drawbot_suiteP->GetSurface(drawRef, &surface));
	if (err) return err;

	const PF_Rect &frame = extra->effect_win.current_frame;

	// Background in AE's panel color.
	PF_App_Color bg = { 0, 0, 0 };
	suites.AppSuite4()->PF_AppGetBgColor(&bg);
	DRAWBOT_ColorRGBA bgC = { bg.red / 65535.0f, bg.green / 65535.0f, bg.blue / 65535.0f, 1.0f };
	DRAWBOT_RectF32 all = { (float)frame.left, (float)frame.top,
							(float)(frame.right - frame.left), (float)(frame.bottom - frame.top) };
	ERR(ds->surface_suiteP->PaintRect(surface, &bgC, &all));

	PF_Handle	arbH = params[LR_GRADIENT]->u.arb_d.value;
	const LRArb	*a = arbH ? reinterpret_cast<const LRArb*>(PF_LOCK_HANDLE(arbH)) : NULL;
	if (!a) return err;

	BarRect br = GetBar(frame);

	// The ramp itself, one slice per 2 px, straight from the render LUT.
	float (*lut)[3] = new float[LR_LUT_SIZE][3];
	LR_BuildLUT(lut, a, params[LR_INTERP]->u.pd.value);
	for (float x = br.l; x < br.r && !err; x += 2.0f) {
		A_long li = (A_long)(XToPos(br, x + 1.0f) * LR_LUT_SIZE);
		if (li >= LR_LUT_SIZE) li = LR_LUT_SIZE - 1;
		DRAWBOT_ColorRGBA c = { lut[li][0], lut[li][1], lut[li][2], 1.0f };
		DRAWBOT_RectF32   s = { x, br.t, 2.0f, br.b - br.t };
		ERR(ds->surface_suiteP->PaintRect(surface, &c, &s));
	}
	delete [] lut;

	// Outline + markers.
	DRAWBOT_ColorRGBA edgeC = { 0.1f, 0.1f, 0.1f, 1.0f };
	DRAWBOT_ColorRGBA ringC = { 0.9f, 0.9f, 0.9f, 1.0f };
	DRAWBOT_PenRef pen = NULL, ring = NULL;
	ERR(ds->supplier_suiteP->NewPen(supplier, &edgeC, 1.0f, &pen));
	ERR(ds->supplier_suiteP->NewPen(supplier, &ringC, 1.0f, &ring));

	DRAWBOT_PathRef path = NULL;
	ERR(ds->supplier_suiteP->NewPath(supplier, &path));
	DRAWBOT_RectF32 barR = { br.l + 0.5f, br.t + 0.5f, br.r - br.l - 1.0f, br.b - br.t - 1.0f };
	ERR(ds->path_suiteP->AddRect(path, &barR));
	ERR(ds->surface_suiteP->StrokePath(surface, pen, path));
	if (path) ds->supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path);

	for (A_long i = 0; i < a->count && !err; ++i) {
		const LRStop &s = a->stops[i];
		float x = floorf(PosToX(br, s.pos)) + 0.5f, y = br.b;

		// A little house shape pointing up at the bar, filled with the stop color.
		path = NULL;
		ERR(ds->supplier_suiteP->NewPath(supplier, &path));
		ERR(ds->path_suiteP->MoveTo(path, x, y + 1));
		ERR(ds->path_suiteP->LineTo(path, x + MARK_HALF, y + 6));
		ERR(ds->path_suiteP->LineTo(path, x + MARK_HALF, y + MARK_H));
		ERR(ds->path_suiteP->LineTo(path, x - MARK_HALF, y + MARK_H));
		ERR(ds->path_suiteP->LineTo(path, x - MARK_HALF, y + 6));
		ERR(ds->path_suiteP->Close(path));

		DRAWBOT_ColorRGBA c = { s.r, s.g, s.b, 1.0f };
		DRAWBOT_BrushRef brush = NULL;
		ERR(ds->supplier_suiteP->NewBrush(supplier, &c, &brush));
		ERR(ds->surface_suiteP->FillPath(surface, brush, path, kDRAWBOT_FillType_Default));
		ERR(ds->surface_suiteP->StrokePath(surface, ring, path));
		if (brush) ds->supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)brush);
		if (path)  ds->supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path);
	}

	if (pen)  ds->supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)pen);
	if (ring) ds->supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)ring);
	PF_UNLOCK_HANDLE(arbH);

	extra->evt_out_flags = PF_EO_HANDLED_EVENT;
	return err;
}

PF_Err
LR_HandleEvent (PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
				PF_LayerDef *output, PF_EventExtra *extra)
{
	PF_Err err = PF_Err_NONE;
	if (extra->effect_win.area != PF_EA_CONTROL) return err;

	switch (extra->e_type) {
	case PF_Event_DRAW:
		err = DrawBar(in_data, out_data, params, extra);
		break;

	case PF_Event_DO_CLICK:
	case PF_Event_DRAG: {
		PF_Boolean changed = FALSE;
		err = DoClickOrDrag(in_data, params, extra, &changed);
		if (!err && changed) {
			AEGP_SuiteHandler suites(in_data->pica_basicP);
			params[LR_GRADIENT]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
			suites.AppSuite4()->PF_InvalidateRect(extra->contextH, NULL);
			extra->evt_out_flags |= PF_EO_UPDATE_NOW;
		}
		break;
	}
	}
	return err;
}
