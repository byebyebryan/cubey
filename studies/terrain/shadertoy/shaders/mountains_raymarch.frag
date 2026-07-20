#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;

layout(push_constant) uniform ReferencePushConstants {
    vec4 resolution_time;
    vec4 mouse;
} reference_push;

#define iResolution reference_push.resolution_time.xyz
#define iTime reference_push.resolution_time.w
#define iMouse reference_push.mouse

#include "mountains.glsl"

void main() {
    const vec2 shader_toy_frag_coord =
        vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y);
    mainImage(out_color, shader_toy_frag_coord);
}
