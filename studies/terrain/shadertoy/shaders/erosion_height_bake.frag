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

#include "eroded_mountains_common.glsl"
#undef TREES
#define mainImage erosion_ignored_main_image
#include "eroded_mountains_buffer_b.glsl"
#undef mainImage

const float kHeightNormalization = 0.001;
const float kFilterBlend = 0.25;

void main() {
    iResolution = bake_push.resolution_time.xyz;
    iTime = bake_push.resolution_time.w;
    iMouse = bake_push.mouse;
    iFrame = 0;
    iFrameRate = 60.0;
    iTimeDelta = 1.0 / 60.0;
    const vec2 uv = gl_FragCoord.xy / iResolution.xy;
    const float source_height = texture(iChannel0, uv).x;
    const float normalized_height = mix(source_height, Heightmap(uv).x, kFilterBlend);
    const float world_height = (normalized_height - DEFAULT_HEIGHT) / kHeightNormalization;
    out_height_fields = vec4(world_height, world_height, world_height, 0.0);
}
