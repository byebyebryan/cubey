#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_AUTHORED
#define SDF_ANALYTIC
#define SDF_ACCELERATION
#define SDF_LIVE_EDIT
#define SDF_LIT_ONLY
#define SDF_MATERIAL_SPECIALIZATION
#define SDF_ZERO_LIGHT_SHADOWS
#include "sdf_surface.glsl"
