#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"

layout(push_constant) uniform SkyParams {
    vec4 camera_time;
    vec4 camera_right_aspect;
    vec4 camera_up_tan_half_fovy;
    vec4 camera_forward;
    vec4 sun_direction;
} sky;

#define OCEAN_ATMOSPHERE_SUN_DIRECTION sky.sun_direction.xyz
#include "ocean_atmosphere.glsl"

layout(location = 0) in vec2 frag_ndc;
layout(location = 0) out vec4 out_color;

void main() {
    float tan_half_fovy = sky.camera_up_tan_half_fovy.w;
    vec3 direction = normalize(sky.camera_forward.xyz +
                               sky.camera_right_aspect.xyz * frag_ndc.x *
                                   sky.camera_right_aspect.w * tan_half_fovy -
                               sky.camera_up_tan_half_fovy.xyz * frag_ndc.y * tan_half_fovy);
    vec3 color = ocean_sky_color(direction);
    out_color = vec4(color, 1.0);
}
