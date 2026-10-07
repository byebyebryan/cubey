#version 450
#extension GL_GOOGLE_include_directive : require
layout(set=0,binding=0,std430) readonly buffer Terrain { float values[]; } terrain;
layout(set=0,binding=1,std430) readonly buffer Depth { float values[]; } depth;
#include "fluid_25d_bspline_surface.glsl"
layout(push_constant) uniform CatchmentParams {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet;
    vec4 presentation; vec4 terrain_palette;
} params;
layout(location=0) out vec3 world_position;
layout(location=1) out vec3 world_normal;
layout(location=2) out vec2 world_xz;
void main() {
    uvec2 grid = uvec2(params.grid_cell.xy);
    // Terrain fragment retains palette.w (speed); spare presentation.x is VS-only.
    uint subdivision = uint(params.presentation.x);
    vec2 p = fluid25d_bspline_vertex(uint(gl_VertexIndex),grid,subdivision);
    vec2 bh,dx,dy;
    fluid25d_bspline_sample(p,grid,bh,dx,dy);
    world_xz = (p-0.5*vec2(grid-uvec2(1)))*params.grid_cell.z;
    world_position = vec3(world_xz.x,bh.x*params.grid_cell.w,world_xz.y);
    world_normal = fluid25d_bspline_normal(dx,dy,params.grid_cell.z,params.grid_cell.w,false);
    gl_Position = params.view_projection*vec4(world_position,1);
}
