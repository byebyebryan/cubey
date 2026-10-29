#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_LIT_ONLY
#define SDF_MATERIAL_SPECIALIZATION
#define SDF_AUTHORED
#define SDF_OPTICS
#define SDF_MATERIAL_STUDY
#define SDF_HYBRID
#include "sdf_surface.glsl"
