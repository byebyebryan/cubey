#version 450

layout(push_constant) uniform TerrainLabPushConstants {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 field_ranges;
    vec4 contribution_ranges;
    vec4 hydrology_ranges;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_terrain;
layout(location = 3) in vec4 in_contributions;
layout(location = 4) in vec4 in_hydrology;
layout(location = 5) in vec4 in_material_a;
layout(location = 6) in vec4 in_material_b;
layout(location = 7) in vec4 in_vegetation;
layout(location = 8) in vec4 in_influences;
layout(location = 9) in vec4 in_feature_tags;
layout(location = 10) in vec4 in_drivers;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec4 frag_terrain;
layout(location = 3) out vec4 frag_contributions;
layout(location = 4) out vec4 frag_hydrology;
layout(location = 5) out vec4 frag_material_a;
layout(location = 6) out vec4 frag_material_b;
layout(location = 7) out vec4 frag_vegetation;
layout(location = 8) out vec4 frag_influences;
layout(location = 9) out vec4 frag_feature_tags;
layout(location = 10) out vec4 frag_drivers;

void main() {
    vec4 world_position = vec4(in_position, 1.0);
    gl_Position = pc.view_projection * world_position;
    frag_world_position = in_position;
    frag_normal = normalize(in_normal);
    frag_terrain = in_terrain;
    frag_contributions = in_contributions;
    frag_hydrology = in_hydrology;
    frag_material_a = in_material_a;
    frag_material_b = in_material_b;
    frag_vegetation = in_vegetation;
    frag_influences = in_influences;
    frag_feature_tags = in_feature_tags;
    frag_drivers = in_drivers;
}
