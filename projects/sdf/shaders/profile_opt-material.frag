#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_TIGHT_ROOT
#define SDF_ACCELERATION
#define SDF_FAR_REGIONS
#define SDF_PROFILE_DEBUG_ZERO
#define SDF_PROFILE_SINGLE_MATERIAL
#define SDF_AUTHORED
#define SDF_ANALYTIC
#define SDF_PRUNED
#define SDF_INVESTIGATE
#define SDF_FIXED_STAGE 3
#include "sdf_surface.glsl"
