#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/pbr.glsl"

layout(set = 0, binding = 0) uniform sampler2D cloud_color_texture;

layout(location = 0) in vec2 frag_position;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = frag_position * 0.5 + 0.5;
    vec3 scene_color = texture(cloud_color_texture, uv).rgb;
    vec3 display = cubey_pbr_apply_display_transform(scene_color, vec4(-1.20, 1.0, 0.0, 0.0));
    out_color = vec4(display, 1.0);
}
