#version 450
#extension GL_GOOGLE_include_directive : require
#define SDF_TIGHT_ROOT
#define SDF_ACCELERATION
#define SDF_FAR_REGIONS
#define SDF_AUTHORED
#define SDF_ANALYTIC
#define SDF_PRUNED
#define SDF_QUERY_VALIDATE
#define SDF_LIT_ONLY
#include "query_validate.glsl"
