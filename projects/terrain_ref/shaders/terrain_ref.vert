#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_engine_reference.glsl"

layout(push_constant) uniform TerrainRefPushConstants {
    mat4 view_projection;
    vec4 light_direction_extent;
    vec4 terrain_params;
    vec4 water_params;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_color;
layout(location = 2) out vec3 frag_normal;

void main() {
    vec2 seed = pc.terrain_params.xy;
    float vertical_scale = pc.terrain_params.z;
    float water_enabled = pc.terrain_params.w;
    float water_height_m = pc.water_params.x;
    vec2 world_xz = in_position.xz;
    float terrain_height = terrain_engine_reference_height(world_xz, seed);
    float water_mask = water_enabled * step(terrain_height, water_height_m);
    float display_height = mix(terrain_height, max(terrain_height, water_height_m), water_mask);
    vec3 normal = terrain_engine_reference_normal(world_xz, seed, vertical_scale);
    normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), water_mask));
    vec3 color = terrain_engine_reference_material_color(terrain_height, normal.y, water_mask);

    vec3 world_position = vec3(in_position.x, display_height * vertical_scale, in_position.z);
    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    frag_world_position = world_position;
    frag_color = color;
    frag_normal = normal;
}
