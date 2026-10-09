#version 450
#extension GL_GOOGLE_include_directive : require
#include "cubey/procedural/random.glsl"
layout(set=0,binding=0,std430) readonly buffer Terrain {float values[];} terrain;
layout(set=0,binding=1,std430) readonly buffer Depth {float values[];} depth;
#include "fluid_25d_bspline_surface.glsl"
layout(push_constant) uniform RainParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_floor; // camera xyz, scene-wide weather floor m
    vec4 clock_viewport; // integrated fall phase seconds, width, height, bspline subdivision
    vec4 volume; // scene-wide height m, visual speed m/s, streak length m, opacity
} params;
layout(location=0) out vec2 streak_uv;
layout(location=1) out vec3 world_position;
layout(location=2) flat out float surface_y;
layout(location=3) flat out float fade;

float surface(vec2 p) {
    uvec2 grid=uvec2(params.grid_cell.xy);
    if(params.clock_viewport.w>0.0) {
        vec2 h=fluid25d_bspline_mesh_bed_depth(p,grid,uint(params.clock_viewport.w));
        return (h.x+max(h.y,0.0))*params.grid_cell.w;
    }
    ivec2 lo=ivec2(floor(p));
    vec2 f=fract(p);
    float h[4];
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 c=min(lo+ivec2(x,y),ivec2(grid)-1);
        uint i=uint(c.y)*grid.x+uint(c.x);
        h[y*2+x]=terrain.values[i]+max(depth.values[i],0.0);
    }
    return (f.x>=f.y ? h[0]*(1.0-f.x)+h[1]*(f.x-f.y)+h[3]*f.y
                        : h[0]*(1.0-f.y)+h[2]*(f.y-f.x)+h[3]*f.x)*params.grid_cell.w;
}
void main() {
    const vec2 corners[6]=vec2[](vec2(-1,0),vec2(1,0),vec2(1,1),
                                vec2(-1,0),vec2(1,1),vec2(-1,1));
    streak_uv=corners[gl_VertexIndex];
    // Fixed world anchors and phases; neither camera nor intensity re-seeds.
    uint seed=uint(gl_InstanceIndex)*7u+0x25d2026u;
    vec2 p=vec2(cubey_proc_hash01_u32(seed),cubey_proc_hash01_u32(seed+1u))*
           (params.grid_cell.xy-1.0);
    surface_y=surface(p);
    float speed=params.volume.y*mix(0.85,1.15,cubey_proc_hash01_u32(seed+2u));
    float phase=fract(cubey_proc_hash01_u32(seed+3u)+params.clock_viewport.x*
                      speed/params.volume.x);
    float head=params.camera_floor.w+params.volume.x*(1.0-phase);
    float tail=head+params.volume.z;
    vec2 xz=(p-0.5*(params.grid_cell.xy-1.0))*params.grid_cell.z;
    vec3 a=vec3(xz.x,head,xz.y),b=vec3(xz.x,tail,xz.y);
    vec4 ca=params.view_projection*vec4(a,1),cb=params.view_projection*vec4(b,1);
    world_position=mix(a,b,streak_uv.y);
    fade=smoothstep(0.0,params.volume.z/params.volume.x,phase)*
         mix(0.65,1.0,cubey_proc_hash01_u32(seed+4u));
    gl_Position=vec4(2,2,2,1);
    if(ca.w<=0 || cb.w<=0) {fade=0; return;}
    vec2 direction=(cb.xy/cb.w-ca.xy/ca.w)*params.clock_viewport.yz;
    float pixels=length(direction);
    if(pixels<0.25) {fade=0; return;}
    // Only width has a small pixel floor. Fall distance, direction, endpoints
    // and clipping stay world-space; top-down views are allowed to see less.
    vec2 normal=vec2(-direction.y,direction.x)/pixels;
    vec4 clip=mix(ca,cb,streak_uv.y);
    clip.xy+=normal*streak_uv.x*1.4/params.clock_viewport.yz*clip.w;
    gl_Position=clip;
}
