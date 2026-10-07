#ifndef FLUID_25D_SURFACE_SAMPLING_GLSL
#define FLUID_25D_SURFACE_SAMPLING_GLSL

// Uniform cubic B-spline approximation (not interpolation at native samples).
// Positive partition-of-unity weights avoid Catmull-Rom depth undershoot.
vec4 fluid25d_bspline_weights(float t) {
    float t2 = t*t, t3 = t2*t;
    return vec4((1.0-t)*(1.0-t)*(1.0-t), 3.0*t3-6.0*t2+4.0,
                -3.0*t3+3.0*t2+3.0*t+1.0, t3) / 6.0;
}

// Components are 00, 10, 01, 11. These are display samples, never field writes.
float fluid25d_triangle_sample(vec4 v, vec2 f) {
    return f.x >= f.y ? v.x*(1.0-f.x) + v.y*(f.x-f.y) + v.w*f.y
                      : v.x*(1.0-f.y) + v.z*(f.y-f.x) + v.w*f.x;
}
float fluid25d_bilinear_sample(vec4 v, vec2 f) {
    return mix(mix(v.x, v.y, f.x), mix(v.z, v.w, f.x), f.y);
}
uvec4 fluid25d_quad_indices(vec2 p, uvec2 grid, out vec2 f) {
    vec2 bounded = clamp(p, vec2(0), vec2(grid-uvec2(1)));
    uvec2 lo = uvec2(floor(bounded));
    uvec2 hi = min(lo+uvec2(1), grid-uvec2(1));
    f = fract(bounded);
    return uvec4(lo.y*grid.x+lo.x, lo.y*grid.x+hi.x,
                 hi.y*grid.x+lo.x, hi.y*grid.x+hi.x);
}
// A strict majority of local wet support is required. Opposite wet corners
// meet at exactly 0.5 and remain separated, rather than joining across a dry
// diagonal. All-wet patches retain ordinary bilinear depth; all-dry patches
// cannot acquire water. This is a conservative DISPLAY footprint, not volume.
float fluid25d_supported_depth(vec4 depths, vec2 f, float wet_threshold) {
    vec4 wet = vec4(greaterThan(depths, vec4(wet_threshold)));
    if (fluid25d_bilinear_sample(wet, f) <= 0.5) return 0.0;
    return fluid25d_bilinear_sample(depths, f);
}

#endif
