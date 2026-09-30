/*
	LoopRamp_Strings.cpp
*/

#include "LoopRamp.h"

typedef struct {
	A_u_long	index;
	A_char		str[256];
} TableString;



TableString		g_strs[StrID_NUMTYPES] = {
	StrID_NONE,						"",
	StrID_Name,						"Loop Ramp",
	StrID_Description,				"Multi-stop gradient ramp with a looping color offset.",
	StrID_Start_Param_Name,			"Start of Ramp",
	StrID_End_Param_Name,			"End of Ramp",
	StrID_Shape_Param_Name,			"Ramp Shape",
	StrID_Shape_Choices,			"Linear|Radial",
	StrID_Offset_Param_Name,		"Offset (%)",
	StrID_Repeat_Param_Name,		"Repeat",
	StrID_Interp_Param_Name,		"Interpolation",
	StrID_Interp_Choices,			"Constant|Linear|Smooth",
	StrID_Gradient_Param_Name,		"Gradient",
	StrID_Scatter_Param_Name,		"Ramp Scatter",
	StrID_Blend_Param_Name,			"Blend With Original",
};


char	*GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}
