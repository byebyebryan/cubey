#version 450

layout(push_constant) uniform TerrainStageProxyPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    vec4 object_translation;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_color;
layout(location = 2) out vec3 frag_normal;

void main() {
    vec3 world_position = in_position + pc.object_translation.xyz;
    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    frag_world_position = world_position;
    frag_color = in_color;
    frag_normal = in_normal;
}
