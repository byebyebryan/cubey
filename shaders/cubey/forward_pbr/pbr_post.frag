#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/pbr.glsl"

layout(set = 0, binding = 0) uniform PbrPostUniforms {
    vec4 display_transform;
} post;

layout(set = 0, binding = 1) uniform sampler2D scene_color;

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 sample_color = texture(scene_color, frag_uv);
    vec3 color = sample_color.rgb;
    out_color = vec4(cubey_pbr_apply_display_transform(color, post.display_transform),
                     sample_color.a);
}
