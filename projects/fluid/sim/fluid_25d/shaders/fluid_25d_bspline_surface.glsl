#ifndef FLUID_25D_BSPLINE_SURFACE_GLSL
#define FLUID_25D_BSPLINE_SURFACE_GLSL
#include "fluid_25d_surface_sampling.glsl"

// Requires readonly terrain/depth buffers declared by the caller. The same
// basis for both fields keeps their displayed gap equal to displayed depth.
// Clamped extension is explicit; no input field or saved state is modified.
vec4 fluid25d_bspline_derivative(float t) {
    float t2=t*t;
    return vec4(-0.5*(1.0-t)*(1.0-t), 1.5*t2-2.0*t,
                -1.5*t2+t+0.5, 0.5*t2);
}
void fluid25d_bspline_sample(vec2 p, uvec2 grid, out vec2 result,
                             out vec2 dx, out vec2 dy) {
    vec2 bounded = clamp(p, vec2(0), vec2(grid-uvec2(1)));
    ivec2 base = ivec2(floor(bounded))-ivec2(1);
    vec4 wx = fluid25d_bspline_weights(fract(bounded).x);
    vec4 wy = fluid25d_bspline_weights(fract(bounded).y);
    vec4 dwx = fluid25d_bspline_derivative(fract(bounded).x);
    vec4 dwy = fluid25d_bspline_derivative(fract(bounded).y);
    result = vec2(0); dx=vec2(0); dy=vec2(0);
    for (int y=0; y<4; ++y) for (int x=0; x<4; ++x) {
        ivec2 cell = clamp(base+ivec2(x,y), ivec2(0), ivec2(grid)-ivec2(1));
        uint i = uint(cell.y)*grid.x+uint(cell.x);
        vec2 value = vec2(terrain.values[i], depth.values[i]);
        result += value * (wx[x]*wy[y]);
        dx += value * (dwx[x]*wy[y]);
        dy += value * (wx[x]*dwy[y]);
    }
}
vec2 fluid25d_bspline_bed_depth(vec2 p, uvec2 grid) {
    vec2 result,dx,dy;
    fluid25d_bspline_sample(p,grid,result,dx,dy);
    return result;
}

// Actual displayed triangle mesh, including its fixed 00->11 diagonal.
// Markers use this, rather than floating on the analytic cubic surface.
vec2 fluid25d_bspline_mesh_bed_depth(vec2 p, uvec2 grid, uint subdivision) {
    float s = float(subdivision);
    vec2 bounded = clamp(p, vec2(0), vec2(grid-uvec2(1)));
    vec2 lo = floor(bounded*s)/s;
    vec2 hi = min(lo+vec2(1.0/s), vec2(grid-uvec2(1)));
    vec2 f = fract(bounded*s);
    vec2 a = fluid25d_bspline_bed_depth(lo, grid);
    vec2 b = fluid25d_bspline_bed_depth(vec2(hi.x,lo.y), grid);
    vec2 c = fluid25d_bspline_bed_depth(vec2(lo.x,hi.y), grid);
    vec2 d = fluid25d_bspline_bed_depth(hi, grid);
    return f.x >= f.y ? a*(1.0-f.x)+b*(f.x-f.y)+d*f.y
                      : a*(1.0-f.y)+c*(f.y-f.x)+d*f.x;
}

vec2 fluid25d_bspline_vertex(uint vertex_index, uvec2 grid, uint subdivision) {
    uint quads_x = (grid.x-1u)*subdivision;
    uint quad = vertex_index/6u;
    const uvec2 corners[6] = uvec2[](uvec2(0,0),uvec2(1,1),uvec2(1,0),
                                    uvec2(0,0),uvec2(0,1),uvec2(1,1));
    return vec2(uvec2(quad%quads_x, quad/quads_x)+corners[vertex_index%6u]) /
           float(subdivision);
}
vec3 fluid25d_bspline_normal(vec2 dx, vec2 dy, float cell_size,
                             float height_scale, bool water) {
    vec2 weights = water ? vec2(1,1) : vec2(1,0);
    return normalize(vec3(-dot(dx,weights)*height_scale,
                          cell_size, -dot(dy,weights)*height_scale));
}
#endif
