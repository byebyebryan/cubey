#version 450
#extension GL_GOOGLE_include_directive : require
#include "fluid_25d_scenic.glsl"
#include "fluid_25d_whitewater_motion.glsl"
layout(push_constant) uniform Params {
    mat4 view_projection; vec4 grid_cell; vec4 camera_wet;
    vec4 clock_shape; vec4 viewport_sampling;
} params;
layout(location=0) in vec2 local_uv;
layout(location=1) in vec3 world_position;
layout(location=2) flat in vec3 facing_normal;
layout(location=3) flat in vec3 life_seed_gain;
layout(location=0) out vec4 out_color;
void main() {
    vec2 uv=gl_FragCoord.xy/params.viewport_sampling.xy;
    if(gl_FragCoord.z>texture(scenic_depth,uv).r) discard;
    float r=length(local_uv);
    float theta=atan(local_uv.y,local_uv.x);
    // Borrow the existing Water3D lobed-disc family, not its packed output or
    // simulation buffers. Here it is lit, premultiplied linear HDR foam.
    float lobe=0.09*sin(theta*5.0+life_seed_gain.y*19.1)+
               0.045*sin(theta*9.0-life_seed_gain.y*31.7);
    float radial=r/(1.0+lobe);
    float aa=max(0.02,fwidth(radial));
    float body=1.0-smoothstep(max(0.0,0.8-aa),min(1.0,0.8+aa),radial);
    float life=fluid25d_whitewater_life(life_seed_gain.x);
    float alpha=0.45*body*life*life_seed_gain.z;
    if(alpha<0.001) discard;
    vec3 n=normalize(facing_normal);
    float direct=max(0.0,dot(n,scenic.light_direction_exposure.xyz));
    vec3 lit=vec3(0.84,0.89,0.87)*(scenic_irradiance_at(n)+
             scenic_sun_radiance()*direct*scenic_sun_visibility(world_position,n)/3.14159265);
    out_color=vec4(lit*alpha,alpha);
}
