#version 450

layout(set = 0, binding = 1) uniform Uniforms {
    mat4 mvp;
} ubo;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_color;

void main() {
    gl_Position = ubo.mvp * vec4(in_position, 1.0);
    v_uv = in_uv;
    v_color = in_color;
}
