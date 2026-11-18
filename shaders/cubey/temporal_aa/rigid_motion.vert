#version 450

layout(std140, set = 0, binding = 0) uniform RigidMotionUniforms {
    mat4 current_jittered_view_projection;
    mat4 current_view_projection;
    mat4 previous_view_projection;
    vec4 extent;
} u;

layout(push_constant) uniform RigidMotionPushConstants {
    mat4 current_model;
    mat4 previous_model;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec3 object_position;

void main() {
    vec4 current_world = push_constants.current_model * vec4(in_position, 1.0);
    gl_Position = u.current_jittered_view_projection * current_world;
    object_position = in_position;
}
