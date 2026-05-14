#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 light_mvp;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 3) in vec2 in_uv0;

layout(location = 0) out vec2 frag_uv0;

void main() {
    gl_Position = push_constants.light_mvp * vec4(in_position, 1.0);
    frag_uv0 = in_uv0;
}
