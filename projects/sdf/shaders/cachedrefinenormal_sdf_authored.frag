#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_CACHED_QUALITY
#define SDF_CACHED_REFINE
#define SDF_CACHED_EPSILON 0.03125
#define SDF_CACHED_NORMAL
#define SDF_ACCELERATION
#define SDF_AUTHORED
#include "sdf_surface.glsl"
