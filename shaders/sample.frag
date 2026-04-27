#version 450

layout(set = 0, binding = 0) uniform sampler2D src_image;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 texel = texture(src_image, v_uv).rgb;
    vec3 lit = texel * (0.35 + 0.65 * v_color);
    out_color = vec4(lit, 1.0);
}
