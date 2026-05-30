#version 450

layout(push_constant) uniform TerrainPushConstants {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 field_ranges;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_material;
layout(location = 3) in vec4 in_fields;
layout(location = 4) in vec4 in_generator;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec4 frag_material;
layout(location = 3) out vec4 frag_fields;
layout(location = 4) out vec4 frag_generator;

void main() {
    vec4 world_position = vec4(in_position, 1.0);
    gl_Position = pc.view_projection * world_position;
    frag_world_position = in_position;
    frag_normal = normalize(in_normal);
    frag_material = in_material;
    frag_fields = in_fields;
    frag_generator = in_generator;
}
