#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_ZERO_LIGHT_SHADOWS
#define SDF_PREPARED_CONES
#define SDF_LIT_ONLY
#define SDF_MATERIAL_SPECIALIZATION
#define SDF_TIGHT_ROOT
#define SDF_ACCELERATION
#define SDF_FAR_REGIONS
#define SDF_AUTHORED
#define SDF_ANALYTIC
#define SDF_PRUNED
#define SDF_GUARDED_STEPS
#define SDF_RELAX_OMEGA 1.5
#include "sdf_surface.glsl"
