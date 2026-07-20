#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 out_height_and_slope;

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform BakePushConstants {
    vec4 resolution_time;
    vec4 mouse;
    vec4 domain_center_extent;
} bake_push;

#define iResolution bake_push.resolution_time.xyz
#define iTime bake_push.resolution_time.w
#define iMouse bake_push.mouse

#include "mountains.glsl"

const float kHeightNormalization = 0.001;
const float kReferenceBaseHeight = 0.45;

void main() {
    const vec2 uv = gl_FragCoord.xy / iResolution.xy;
    const vec2 world = bake_push.domain_center_extent.xy +
        (uv - 0.5) * bake_push.domain_center_extent.z;
    const vec2 texel = 1.0 / iResolution.xy;
    const vec2 world_step = texel * bake_push.domain_center_extent.z;
    const float center = Terrain(world);
    const float dx =
        (Terrain(world + vec2(world_step.x, 0.0)) -
         Terrain(world - vec2(world_step.x, 0.0))) *
        kHeightNormalization / (2.0 * texel.x);
    const float dz =
        (Terrain(world + vec2(0.0, world_step.y)) -
         Terrain(world - vec2(0.0, world_step.y))) *
        kHeightNormalization / (2.0 * texel.y);
    out_height_and_slope =
        vec4(kReferenceBaseHeight + center * kHeightNormalization, dx, dz, 0.0);
}
