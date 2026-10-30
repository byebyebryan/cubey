#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_AUTHORED
#define SDF_OPTICS
#define SDF_MATERIAL_STUDY
#define SDF_CACHED_QUALITY
#define SDF_CACHED_REFINE
#define SDF_CACHED_EPSILON 0.03125
#include "sdf_surface.glsl"
