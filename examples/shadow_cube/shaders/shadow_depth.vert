#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 light_mvp;
} push_constants;

layout(location = 0) in vec3 in_position;

void main() {
    gl_Position = push_constants.light_mvp * vec4(in_position, 1.0);
}
