#version 450

layout(push_constant) uniform TerrainBackdropPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    vec4 render_options;
    vec4 material_options;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_surface_channels;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_material_channels;
layout(location = 2) out vec3 frag_normal;
layout(location = 3) out vec2 frag_surface_channels;

void main() {
    gl_Position = pc.view_projection * vec4(in_position, 1.0);
    frag_world_position = in_position;
    frag_material_channels = in_color;
    frag_normal = in_normal;
    frag_surface_channels = in_surface_channels;
}
