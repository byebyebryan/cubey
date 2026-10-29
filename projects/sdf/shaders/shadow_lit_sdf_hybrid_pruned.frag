#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_LIT_ONLY
#define SDF_ZERO_LIGHT_SHADOWS
#define SDF_MATERIAL_SPECIALIZATION
#define SDF_AUTHORED
#define SDF_HYBRID
#define SDF_PRUNED
#include "sdf_surface.glsl"
