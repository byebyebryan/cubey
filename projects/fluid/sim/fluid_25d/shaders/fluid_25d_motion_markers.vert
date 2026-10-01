#version 450
#extension GL_GOOGLE_include_directive : require
#include "fluid_25d_motion_markers.glsl"

layout(set=0,binding=0,std430) readonly buffer Terrain {float values[];} terrain;
layout(set=0,binding=1,std430) readonly buffer Depth {float values[];} depth;
layout(set=0,binding=2,std430) readonly buffer Markers {MotionMarker values[];} markers;
layout(set=0,binding=3,std430) readonly buffer Status {uvec4 values[];} status;
layout(push_constant) uniform RenderParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 display;
    vec4 timing;
} params;
layout(location=0) out vec2 local_position;
layout(location=1) out float opacity;
layout(location=2) flat out uint dot_shape;

float surface(vec2 p) {
    vec2 bounded=clamp(p,vec2(0),params.grid_cell.xy-vec2(1));
    ivec2 lower=ivec2(floor(bounded));
    vec2 f=fract(bounded);
    float h[4];
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 cell=min(lower+ivec2(x,y),ivec2(params.grid_cell.xy)-ivec2(1));
        uint i=uint(cell.y)*uint(params.grid_cell.x)+uint(cell.x);
        h[y*2+x]=terrain.values[i]+depth.values[i];
    }
    // Match water.vert's two triangles (00,11,10) and (00,01,11).
    // Bilinear heights can sit below that visible mesh on curved cells,
    // causing apparently flickering markers despite stable advection.
    float triangulated=f.x>=f.y
        ? h[0]*(1.0-f.x)+h[1]*(f.x-f.y)+h[3]*f.y
        : h[0]*(1.0-f.y)+h[2]*(f.y-f.x)+h[3]*f.x;
    return triangulated*params.grid_cell.w;
}
bool wet(vec2 p) {
    if(any(lessThan(p,vec2(-0.5))) || any(greaterThanEqual(p,params.grid_cell.xy-vec2(0.5))))return false;
    ivec2 cell=ivec2(floor(p+vec2(0.5)));
    return depth.values[uint(cell.y)*uint(params.grid_cell.x)+uint(cell.x)]>params.display.w;
}
vec4 clip_position(vec2 p) {
    vec2 xz=(p-0.5*(params.grid_cell.xy-vec2(1)))*params.grid_cell.z;
    vec4 clip=params.view_projection*vec4(xz.x,surface(p),xz.y,1.0);
    clip.z-=12.0*1.19209290e-7*clip.w;
    return clip;
}
void main() {
    MotionMarker marker=markers.values[uint(gl_InstanceIndex)];
    uint segment=uint(gl_VertexIndex)/6u;
    uint vertex=uint(gl_VertexIndex)%6u;
    const vec2 corners[6]=vec2[](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    local_position=corners[vertex];
    dot_shape=segment==MARKER_TRAIL_POINTS?1u:0u;
    opacity=0.0;
    gl_Position=vec4(2,2,2,1);
    if(marker.position_age_active.w<0.5)return;
    float phase=status.values[0].x==0u?clamp(params.display.z,0,1):1.0;
    vec2 current=mix(marker.previous_xy_reserved.xy,marker.position_age_active.xy,phase);
    if(!wet(current))return;
    float birth_fade=clamp((marker.position_age_active.z+2.0)/20.0,0.15,1.0);
    if(dot_shape==1u) {
        vec4 clip=clip_position(current);
        clip.xy+=local_position*(vec2(2.8)/params.display.xy)*clip.w;
        gl_Position=clip;
        opacity=0.95*birth_fade;
        return;
    }
    float displayed_time=params.timing.x-params.timing.y+phase*params.timing.y;
    uint history_offset=marker.history[0].z>displayed_time?1u:0u;
    if(segment+history_offset>=MARKER_TRAIL_POINTS)return;
    vec4 previous=marker.history[segment+history_offset];
    vec4 next=segment==0u?vec4(current,0,1):marker.history[segment+history_offset-1u];
    if(previous.w<0.5 || next.w<0.5 || !wet(previous.xy) || !wet(next.xy))return;
    // Retained trail points can become separated by newly dry cells. Sample
    // intermediate support before drawing, so trails do not bridge a bank.
    int support_samples=max(1,int(ceil(length(next.xy-previous.xy)*4.0)));
    // Very long straight chords are not a useful representation of a curved
    // trajectory. Omit them rather than skipping dry support to cap the work.
    if(support_samples>128)return;
    for(int i=1;i<support_samples;++i)
        if(!wet(mix(previous.xy,next.xy,float(i)/float(support_samples))))return;
    vec4 a=clip_position(previous.xy),b=clip_position(next.xy);
    if(a.w<=0 || b.w<=0)return;
    vec2 direction=(b.xy/b.w-a.xy/a.w)*params.display.xy;
    float length_pixels=length(direction);
    if(length_pixels<0.5)return;
    vec2 normal=vec2(-direction.y,direction.x)/length_pixels;
    vec4 clip=local_position.x<0? a:b;
    clip.xy+=normal*local_position.y*(vec2(1.35)/params.display.xy)*clip.w;
    gl_Position=clip;
    opacity=(0.36-0.045*float(segment))*birth_fade;
}
