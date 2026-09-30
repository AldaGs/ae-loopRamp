/*
	LoopRamp.h

	A better Gradient Ramp: any number of color stops edited in a gradient bar
	in the Effect Controls panel, linear or radial, Ramp Scatter to hide banding,
	and an Offset that slides the colors along the ramp and WRAPS, so animating
	it cycles the colors forever.

	The stops live in ONE arbitrary-data param (LRArb): AE saves, copies and
	keyframes it through the callbacks in LoopRamp_UI.cpp, which also draws and
	edits the bar (custom ECW UI).
*/

#pragma once

#ifndef LOOPRAMP_H
#define LOOPRAMP_H

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
#include "AE_EffectUI.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEFX_SuiteHelper.h"
#include "AEGP_SuiteHandler.h"

#include "LoopRamp_Strings.h"

#define	MAJOR_VERSION	1
#define	MINOR_VERSION	1
#define	BUG_VERSION		2
#define	STAGE_VERSION	PF_Stage_DEVELOP
#define	BUILD_VERSION	1

#define	LR_LUT_SIZE		1024

#define	LR_SHAPE_LINEAR		1
#define	LR_SHAPE_RADIAL		2

#define	LR_INTERP_CONSTANT	1
#define	LR_INTERP_LINEAR	2
#define	LR_INTERP_SMOOTH	3

/* Parameter order. MUST match the PF_ADD_* order in ParamsSetup. */
enum {
	LR_INPUT = 0,
	LR_GRADIENT,		// arb data: the stop list + its editor bar
	LR_START,
	LR_END,
	LR_SHAPE,
	LR_OFFSET,
	LR_REPEAT,
	LR_SCATTER,
	LR_INTERP,
	LR_BLEND,
	LR_NUM_PARAMS
};

/* Disk IDs: append only, never renumber. 7 (Stop Count) and 100+ (the old
   fixed Color/Position pairs of v1.0) are retired - do not reuse them. */
enum {
	START_DISK_ID = 1,
	END_DISK_ID,
	SHAPE_DISK_ID,
	OFFSET_DISK_ID,
	REPEAT_DISK_ID,
	INTERP_DISK_ID,
	COUNT_DISK_ID_RETIRED,
	BLEND_DISK_ID,
	GRADIENT_DISK_ID,
	SCATTER_DISK_ID,
};

/* ---- the stop list (arbitrary data) ----------------------------------- */

// ponytail: fixed-capacity handle so in-place edits never resize it. Only
// `count` stops are flattened to disk, so raising the cap later stays
// compatible with saved projects.
#define	LR_ARB_CAP		256
#define	LR_ARB_MAGIC	0x4C524D31		// 'LRM1'
#define	LR_ARB_REFCON	((void*)0x4C52414D)

typedef struct {
	float		pos;			// 0..1 along one period
	float		r, g, b;		// 0..1
} LRStop;

typedef struct {
	A_long		magic;
	A_long		count;			// 2..LR_ARB_CAP; stored UNSORTED (UI order)
	LRStop		stops[LR_ARB_CAP];
} LRArb;

#define	LR_ARB_FLAT_SIZE(n)		(2 * sizeof(A_long) + (n) * sizeof(LRStop))

/* ---- per-render snapshot ---------------------------------------------- */

typedef struct LRInfo {
	// geometry, in FULL-RES layer pixels
	PF_FpLong	sx, sy;				// start point
	PF_FpLong	dx, dy;				// end - start
	PF_FpLong	invLen2;			// 1 / |d|^2 (linear)
	PF_FpLong	invLen;				// 1 / |d|   (radial)
	PF_FpLong	originX, originY;	// downsampled layer pixel of world pixel (0,0)
	PF_FpLong	invDsx, invDsy;		// downsampled -> full-res scale (1, 2, 4...)
	A_long		shape;
	PF_FpLong	offset;				// in cycles (slider % / 100)
	PF_FpLong	repeat;
	PF_FpLong	scatter;			// noise amplitude, in cycles
	PF_FpLong	blendF;				// 0..1
	float		lut[LR_LUT_SIZE][3];	// one full period, t in [0,1)
} LRInfo;

/* Shared between LoopRamp.cpp (render) and LoopRamp_UI.cpp (editor bar). */
void	LR_BuildLUT(float lut[][3], const LRArb *arb, A_long interp);

PF_Err	LR_NewDefaultArb(PF_InData *in_data, PF_ArbitraryH *arbPH);
PF_Err	LR_HandleArbitrary(PF_InData *in_data, PF_OutData *out_data, PF_ArbParamsExtra *extra);
PF_Err	LR_HandleEvent(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
					   PF_LayerDef *output, PF_EventExtra *extra);


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

#endif // LOOPRAMP_H
