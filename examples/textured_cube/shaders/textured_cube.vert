#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) out vec3 frag_normal;

void main() {
    gl_Position = pc.mvp * vec4(in_position, 1.0);
    frag_color = in_color;
    frag_uv = in_uv;
    frag_normal = normalize(mat3(pc.model) * in_normal);
}
