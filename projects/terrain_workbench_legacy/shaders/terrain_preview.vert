#version 450

layout(push_constant) uniform TerrainPreviewPushConstants {
    mat4 view_projection;
    vec4 light_direction_extent;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_color;
layout(location = 2) out vec3 frag_normal;

void main() {
    gl_Position = pc.view_projection * vec4(in_position, 1.0);
    frag_world_position = in_position;
    frag_color = in_color;
    frag_normal = normalize(in_normal);
}
