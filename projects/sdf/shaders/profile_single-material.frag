#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_TIGHT_ROOT
#define SDF_ACCELERATION
#define SDF_FAR_REGIONS
#define SDF_PROFILE_SINGLE_MATERIAL
#define SDF_AUTHORED
#define SDF_ANALYTIC
#define SDF_PRUNED
#include "sdf_surface.glsl"
