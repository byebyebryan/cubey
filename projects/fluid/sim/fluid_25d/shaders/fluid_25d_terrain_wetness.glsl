#ifndef FLUID_25D_TERRAIN_WETNESS_GLSL
#define FLUID_25D_TERRAIN_WETNESS_GLSL
#include "fluid_25d_bspline_surface.glsl"

// Requires the readonly terrain/depth buffers. Wet-ground shading must follow
// the selected display reconstruction, not expose native cell triangles below
// a smooth water surface. Cubic sampling is continuous and independent of the
// display mesh subdivision; it changes neither native depth nor water coverage.
float fluid25d_terrain_wet_depth(vec2 coordinate, uvec2 grid, bool bspline) {
    if (bspline)
        return max(0.0,fluid25d_bspline_bed_depth(coordinate,grid).y);
    vec2 f;
    uvec4 i = fluid25d_quad_indices(coordinate,grid,f);
    return max(0.0,fluid25d_triangle_sample(vec4(depth.values[i.x],depth.values[i.y],
                                               depth.values[i.z],depth.values[i.w]),f));
}

// Retain the existing current-film interpretation and thresholds. This same
// weight drives both colour and roughness; no persistent wetness is invented.
float fluid25d_terrain_wet_weight(float h, float wet_threshold) {
    return smoothstep(wet_threshold,0.012,h)*(1.0-smoothstep(0.02,0.05,h));
}
#endif
