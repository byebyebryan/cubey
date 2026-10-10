#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/procedural/random.glsl"
#include "fluid_25d_water_rapids.glsl"
#include "fluid_25d_whitewater_motion.glsl"
layout(set=0,binding=0,std430) readonly buffer Terrain {float values[];} terrain;
layout(set=0,binding=1,std430) readonly buffer Depth {float values[];} depth;
layout(set=0,binding=2,std430) readonly buffer NativeVelocity {vec4 values[];} native_velocity;
layout(set=1,binding=9,std430) readonly buffer VisualVelocity {vec4 values[];} visual_velocity;
layout(set=0,binding=8,std430) readonly buffer DisplayCoverage {vec4 grid; float values[];} display_coverage;
struct WhitewaterSeed {uint cell; uint slot; float weight; float padding;};
layout(set=2,binding=0,std430) readonly buffer Seeds {WhitewaterSeed values[];} sources;
#include "fluid_25d_bspline_surface.glsl"
layout(push_constant) uniform Params {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet;
    vec4 clock_shape; vec4 viewport_sampling;
} params;
layout(location=0) out vec2 local_uv;
layout(location=1) out vec3 world_position;
layout(location=2) flat out vec3 facing_normal;
layout(location=3) flat out vec3 life_seed_gain;

void sample_surface(vec2 p, out vec2 zh, out vec3 n, out vec2 u) {
    vec2 f; uvec2 grid=uvec2(params.grid_cell.xy);
    uvec4 i=fluid25d_quad_indices(p,grid,f);
    vec4 z=vec4(terrain.values[i.x],terrain.values[i.y],terrain.values[i.z],terrain.values[i.w]);
    vec4 h=vec4(depth.values[i.x],depth.values[i.y],depth.values[i.z],depth.values[i.w]);
    uint options=uint(params.viewport_sampling.z);
    if ((options&16u)!=0u) {
        vec2 analytic,dx,dy;
        fluid25d_bspline_sample(p,grid,analytic,dx,dy);
        zh=fluid25d_bspline_mesh_bed_depth(p,grid,(options>>5u)&7u);
        n=fluid25d_bspline_normal(dx,dy,params.grid_cell.z,params.grid_cell.w,true);
    } else {
        zh=vec2(fluid25d_triangle_sample(z,f),fluid25d_triangle_sample(h,f));
        vec4 s=z+h;
        vec2 grad=f.x>=f.y ? vec2(s.y-s.x,s.w-s.y) : vec2(s.w-s.z,s.z-s.x);
        n=normalize(vec3(-grad.x*params.grid_cell.w,params.grid_cell.z,-grad.y*params.grid_cell.w));
    }
    // Match displayed wet support without moving the bed+h geometric surface.
    float support=zh.y;
    if ((options&8u)!=0u) support=fluid25d_supported_depth(h,f,params.camera_wet.w);
    if ((options&16u)!=0u && fluid25d_bilinear_sample(vec4(greaterThan(h,vec4(params.camera_wet.w))),f)<=0.5)
        support=0.0;
    float coverage=1.0;
    if ((options&512u)!=0u) {
        vec2 cf;
        uvec4 ci=fluid25d_quad_indices(p*display_coverage.grid.z,uvec2(display_coverage.grid.xy),cf);
        coverage=clamp(fluid25d_bilinear_sample(vec4(display_coverage.values[ci.x],display_coverage.values[ci.y],
            display_coverage.values[ci.z],display_coverage.values[ci.w]),cf),0.0,1.0);
    }
    if (support<=params.camera_wet.w || coverage<=0.001) zh.y=0.0;
    u=vec2(fluid25d_bilinear_sample(vec4(visual_velocity.values[i.x].x,visual_velocity.values[i.y].x,
        visual_velocity.values[i.z].x,visual_velocity.values[i.w].x),f),
        fluid25d_bilinear_sample(vec4(visual_velocity.values[i.x].y,visual_velocity.values[i.y].y,
        visual_velocity.values[i.z].y,visual_velocity.values[i.w].y),f));
    vec2 current=vec2(fluid25d_bilinear_sample(vec4(native_velocity.values[i.x].x,native_velocity.values[i.y].x,
        native_velocity.values[i.z].x,native_velocity.values[i.w].x),f),
        fluid25d_bilinear_sample(vec4(native_velocity.values[i.x].y,native_velocity.values[i.y].y,
        native_velocity.values[i.z].y,native_velocity.values[i.w].y),f));
    // A lagging placement job/filter must never keep stopped water moving or
    // advect opposite to the newly accepted native flow. Other details retain
    // the existing filter; this guard is local to the secondary layer.
    u=fluid25d_whitewater_current_flow(current,u);
}
void main() {
    const vec2 corners[6]=vec2[](vec2(-1,-1),vec2(1,1),vec2(1,-1),
                                vec2(-1,-1),vec2(-1,1),vec2(1,1));
    local_uv=corners[gl_VertexIndex];
    gl_Position=vec4(2,2,2,1); world_position=vec3(0); facing_normal=vec3(0,1,0);
    life_seed_gain=vec3(0);
    WhitewaterSeed source=sources.values[gl_InstanceIndex];
    if(source.weight<0.001) return;
    uint seed=(source.cell*32u+source.slot)*7u+0x25d2026u;
    vec2 p=vec2(source.cell%uint(params.grid_cell.x),source.cell/uint(params.grid_cell.x))+
           vec2(cubey_proc_hash01_u32(seed),cubey_proc_hash01_u32(seed+1u));
    float age=fluid25d_whitewater_age(params.clock_shape.x,seed);
    vec2 zh,u; vec3 n; sample_surface(p,zh,n,u);
    float gain=fluid25d_rapid_activity(n,u,zh.y,params.grid_cell.w,params.camera_wet.w)*
               fluid25d_cascade_support(zh.y,params.camera_wet.w);
    // Analytic short-lived flecks: current filtered flow, not transported mass.
    p+=fluid25d_whitewater_offset(u,age,params.viewport_sampling.w,params.grid_cell.z);
    if(any(lessThan(p,vec2(0))) || any(greaterThan(p,params.grid_cell.xy-1.0))) return;
    sample_surface(p,zh,n,u);
    gain*=fluid25d_rapid_activity(n,u,zh.y,params.grid_cell.w,params.camera_wet.w)*
          fluid25d_cascade_support(zh.y,params.camera_wet.w)*source.weight;
    if(gain<0.001) return;
    vec2 xz=(p-0.5*(params.grid_cell.xy-1.0))*params.grid_cell.z;
    vec3 center=vec3(xz.x,(zh.x+zh.y)*params.grid_cell.w,xz.y)+
                n*(0.25+sin(age*3.14159265)*params.clock_shape.w);
    vec3 axis=normalize(fluid25d_cascade_flow(n,u,params.grid_cell.w));
    vec3 view=normalize(params.camera_wet.xyz-center);
    vec3 side=cross(view,axis);
    if(length(side)<0.001) return;
    side=normalize(side); facing_normal=normalize(cross(axis,side));
    if(dot(facing_normal,view)<0) facing_normal=-facing_normal;
    float radius=params.clock_shape.z*mix(0.75,1.25,cubey_proc_hash01_u32(seed+3u));
    world_position=center+side*local_uv.x*radius+axis*local_uv.y*radius*1.8;
    life_seed_gain=vec3(age,cubey_proc_hash01_u32(seed+4u),gain*params.clock_shape.y);
    gl_Position=params.view_projection*vec4(world_position,1);
}
