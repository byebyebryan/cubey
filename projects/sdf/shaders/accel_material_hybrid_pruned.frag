#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_TIGHT_ROOT
#define SDF_AUTHORED
#define SDF_OPTICS
#define SDF_MATERIAL_STUDY
#define SDF_HYBRID
#define SDF_PRUNED
#include "sdf_surface.glsl"
