#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif

resource 'PiPL' (16000) {
	{	/* array properties: 12 elements */
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"Loop Ramp"
		},
		/* [3] */
		Category {
			"Learning"
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {
			561153	/* PF_VERSION(1,1,2,DEVELOP,1): 524288 + (1<<15) + (2<<11) + build 1 */
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
		0x02008040	// DEEP_COLOR_AWARE (1<<25) | CUSTOM_UI (1<<15) | USE_OUTPUT_EXTENT (1<<6)
		},
		AE_Effect_Global_OutFlags_2 {
		0x08001400	// THREADED (1<<27) | FLOAT_COLOR_AWARE (1<<12) | SMART_RENDER (1<<10)
		},
		/* [11] */
		AE_Effect_Match_Name {
			"aldai LoopRamp"
		},
		/* [12] */
		AE_Reserved_Info {
			0
		},
		/* [13] */
		AE_Effect_Support_URL {
			"https://www.adobe.com"
		}
	}
};
