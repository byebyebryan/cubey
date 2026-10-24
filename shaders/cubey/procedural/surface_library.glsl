#ifndef CUBEY_SURFACE_LIBRARY_GLSL
#define CUBEY_SURFACE_LIBRARY_GLSL
#include "cubey/procedural/surface_material.glsl"
// Matches procedural::SurfaceLibrary. Parameter vectors are named per family;
// none of the legacy MaterialPattern components change their meaning.
struct SurfaceLibrary {
    vec4 settings, secondary;
    vec4 wood; // ring frequency, distortion, grain frequency, pore strength
    vec4 rust; // coverage, patch frequency, edge breakup, pit strength
    vec4 weave; // yarn spacing, width, irregularity, fiber strength
    vec4 paper; // fiber frequency, directionality, fleck strength, fiber strength
    vec4 rust_roughness, sheen, optical_options;
};
#endif
