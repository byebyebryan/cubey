#ifndef SDF_SURFACE_PATTERN_GLSL
#define SDF_SURFACE_PATTERN_GLSL
#include "cubey/procedural/surface_material.glsl"
#ifndef AUTHORED_PATTERN_BINDING
#define AUTHORED_PATTERN_BINDING 6
#endif
layout(std430,set=0,binding=AUTHORED_PATTERN_BINDING) readonly buffer AuthoredPatterns {
    MaterialPattern authoredPatterns[];
};
#endif
