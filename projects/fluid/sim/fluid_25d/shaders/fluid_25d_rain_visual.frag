#version 450
#extension GL_GOOGLE_include_directive : require
#include "fluid_25d_scenic.glsl"
layout(push_constant) uniform RainParams {
    mat4 view_projection;
    vec4 grid_cell;
    vec4 camera_floor;
    vec4 clock_viewport;
    vec4 volume;
} params;
layout(location=0) in vec2 streak_uv;
layout(location=1) in vec3 world_position;
layout(location=2) flat in float surface_y;
layout(location=3) flat in float fade;
layout(location=0) out vec4 out_color;
void main() {
    if(fade<=0 || world_position.y<=surface_y) discard;
    vec2 uv=gl_FragCoord.xy/params.clock_viewport.yz;
    float scene_z=texture(scenic_depth,uv).r;
    // Exact hard occlusion behind ridges. A small world-space soft fade avoids
    // harsh contact at the opaque mesh without leaking through foreground.
    if(gl_FragCoord.z>scene_z) discard;
    float contact=smoothstep(0.0,max(0.2,params.volume.z*0.12),world_position.y-surface_y);
    if(scene_z<1.0) {
        vec3 opaque=scenic_unproject(uv,scene_z);
        contact*=smoothstep(0.0,max(0.2,params.volume.z*0.10),
                            distance(opaque,params.camera_floor.xyz)-
                            distance(world_position,params.camera_floor.xyz));
    }
    float alpha=params.volume.w*fade*contact*pow(max(0.0,1.0-abs(streak_uv.x)),1.5)*
                smoothstep(0.0,0.16,streak_uv.y)*(1.0-smoothstep(0.65,1.0,streak_uv.y));
    // Artistic contrast cue: pale over terrain, a neutral silhouette over the
    // bright daylight sky. Increasing density alone cannot distinguish pale
    // rain from a pale sky. No sky tint/exposure/storm lighting is changed.
    vec3 light=clamp(scenic_irradiance_at(vec3(0,1,0))*0.35,vec3(0.3),vec3(1.4));
    if(scene_z>=1.0) light=vec3(0.045);
    out_color=vec4(light*alpha,alpha);
}
