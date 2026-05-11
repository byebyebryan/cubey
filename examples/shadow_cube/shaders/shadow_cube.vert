#version 450

layout(push_constant) uniform ScenePushConstants {
    mat4 mvp;
    mat4 light_mvp;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec4 frag_shadow_position;

void main() {
    gl_Position = push_constants.mvp * vec4(in_position, 1.0);
    frag_color = in_color;
    frag_normal = normalize(in_normal);
    frag_shadow_position = push_constants.light_mvp * vec4(in_position, 1.0);
}
