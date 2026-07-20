#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 out_height_fields;

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform BakePushConstants {
    vec4 resolution_time;
    vec4 mouse;
    vec4 domain_center_extent;
} bake_push;

vec3 iResolution;
float iTime;
vec4 iMouse;
int iFrame;
float iFrameRate;
float iTimeDelta;

#include "swiss_alps_common.glsl"
#define mainImage swiss_alps_ignored_main_image
#include "swiss_alps_buffer_b.glsl"
#undef mainImage

void main() {
    iResolution = bake_push.resolution_time.xyz;
    iTime = bake_push.resolution_time.w;
    iMouse = bake_push.mouse;
    iFrame = 0;
    iFrameRate = 60.0;
    iTimeDelta = 1.0 / 60.0;
    const vec2 uv = gl_FragCoord.xy / iResolution.xy;
    const vec2 world = bake_push.domain_center_extent.xy +
        (uv - 0.5) * bake_push.domain_center_extent.z;
    const float geometry_height = terrainFbm(world, MQ_OCTAVES, iChannel0);
    const float detailed_height = terrainFbm(world, HQ_OCTAVES, iChannel0);
    out_height_fields = vec4(geometry_height, geometry_height, detailed_height, 0.0);
}
