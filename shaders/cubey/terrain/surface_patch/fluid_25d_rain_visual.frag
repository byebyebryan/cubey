#version 450
// Shared paired terrain/water renderer; retained names preserve shader ABI.
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
    // Macro presentation only: soften the finite weather box rather than
    // drawing its top and perimeter as a dense wall against the sky.
    if(scenic.environment_transition.z>0.0) {
        vec2 span=max((params.grid_cell.xy-1.0)*params.grid_cell.z,vec2(1));
        vec2 p=world_position.xz/span+0.5;
        float edge=min(min(p.x,1.0-p.x),min(p.y,1.0-p.y));
        float altitude=(world_position.y-params.camera_floor.w)/params.volume.x;
        float domain=max(span.x,span.y);
        float envelope=smoothstep(0.0,0.075,edge)*
                       (1.0-smoothstep(0.55,1.0,altitude))*
                       (1.0-smoothstep(domain*0.65,domain*1.55,
                                      distance(world_position,params.camera_floor.xyz)));
        if(scene_z>=1.0) envelope*=0.10;
        alpha*=mix(1.0,envelope,scenic.environment_transition.z);
    }
    // Artistic contrast cue: pale over terrain, a neutral silhouette over the
    // bright daylight sky. Increasing density alone cannot distinguish pale
    // rain from a pale sky. No sky tint/exposure/storm lighting is changed.
    vec3 light=clamp(scenic_irradiance_at(vec3(0,1,0))*0.35,vec3(0.3),vec3(1.4));
    if(scene_z>=1.0) light=vec3(0.045);
    if(scenic.environment_transition.x>0.5) {
        light=clamp(scenic_irradiance_at(vec3(0,1,0))*0.35,vec3(0),vec3(1.4));
        if(scene_z>=1.0) light*=0.15;
    }
    out_color=vec4(light*alpha,alpha);
}
