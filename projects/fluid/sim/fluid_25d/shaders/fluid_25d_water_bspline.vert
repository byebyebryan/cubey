#version 450
#extension GL_GOOGLE_include_directive : require
layout(set=0,binding=0,std430) readonly buffer Terrain { float values[]; } terrain;
layout(set=0,binding=1,std430) readonly buffer Depth { float values[]; } depth;
layout(set=0,binding=2,std430) readonly buffer Velocity { vec4 values[]; } velocity;
layout(set=0,binding=3,std430) readonly buffer CueA { float values[]; } cue_a;
layout(set=0,binding=4,std430) readonly buffer CueB { float values[]; } cue_b;
layout(set=0,binding=6,std430) readonly buffer Tracer { float values[]; } tracer_q;
#include "fluid_25d_bspline_surface.glsl"
layout(push_constant) uniform CatchmentParams {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet;
    vec4 presentation; vec4 terrain_palette;
} params;
layout(location=0) out vec3 world_position;
layout(location=1) out vec3 world_normal;
layout(location=2) out float water_depth;
layout(location=3) out vec3 water_flow;
layout(location=4) out float presentation_cue;
layout(location=5) out float dye_concentration;
layout(location=6) out vec2 field_coordinate;
void main() {
    uvec2 grid = uvec2(params.grid_cell.xy);
    uint subdivision = (uint(params.terrain_palette.w)>>5u)&7u;
    field_coordinate = fluid25d_bspline_vertex(uint(gl_VertexIndex),grid,subdivision);
    vec2 bh,dx,dy;
    fluid25d_bspline_sample(field_coordinate,grid,bh,dx,dy);
    vec2 xz = (field_coordinate-0.5*vec2(grid-uvec2(1)))*params.grid_cell.z;
    water_depth = bh.y;
    world_position = vec3(xz.x,(bh.x+bh.y)*params.grid_cell.w,xz.y);
    world_normal = fluid25d_bspline_normal(dx,dy,params.grid_cell.z,
                                          params.grid_cell.w,true);
    // Existing visual fields remain native bilinear reads, not new transport.
    vec2 f;
    uvec4 i = fluid25d_quad_indices(field_coordinate,grid,f);
    water_flow = mix(mix(velocity.values[i.x].xyz,velocity.values[i.y].xyz,f.x),
                     mix(velocity.values[i.z].xyz,velocity.values[i.w].xyz,f.x),f.y);
    vec4 cues = params.presentation.x > 0.5
        ? vec4(cue_a.values[i.x],cue_a.values[i.y],cue_a.values[i.z],cue_a.values[i.w])
        : vec4(cue_b.values[i.x],cue_b.values[i.y],cue_b.values[i.z],cue_b.values[i.w]);
    presentation_cue = fluid25d_bilinear_sample(cues,f);
    float q = fluid25d_bilinear_sample(vec4(tracer_q.values[i.x],tracer_q.values[i.y],
                                           tracer_q.values[i.z],tracer_q.values[i.w]),f);
    // Native concentration read stays separate from reconstructed display depth.
    float h = fluid25d_bilinear_sample(vec4(depth.values[i.x],depth.values[i.y],
                                           depth.values[i.z],depth.values[i.w]),f);
    dye_concentration = clamp(max(0.0,q)/max(h,1e-6),0,1);
    gl_Position = params.view_projection*vec4(world_position,1);
    gl_Position.z -= 8.0*1.19209290e-7*gl_Position.w;
}
