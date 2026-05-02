#version 450

layout(set = 0, binding = 0) uniform sampler2D cube_texture;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 sampled = texture(cube_texture, frag_uv);
    out_color = vec4(frag_color, 1.0) * sampled;
}
