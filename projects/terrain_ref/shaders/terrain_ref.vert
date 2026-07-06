#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_engine_reference.glsl"

layout(set = 0, binding = 0, std140) uniform TerrainRefFrame {
    mat4 view_projection;
    vec4 light_direction_extent;
    vec4 terrain_params;
    vec4 water_params;
    vec4 camera_position_fog;
    vec4 material_params;
} frame;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_material_uv;
layout(location = 3) out float frag_height_m;
layout(location = 4) out float frag_water_mask;

void main() {
    vec2 seed = frame.terrain_params.xy;
    float vertical_scale = frame.terrain_params.z;
    float water_enabled = frame.terrain_params.w;
    float water_height_m = frame.water_params.x;
    vec2 world_xz = in_position.xz;
    float terrain_height = terrain_engine_reference_height(world_xz, seed);
    float water_mask = water_enabled * step(terrain_height, water_height_m);
    float display_height = mix(terrain_height, max(terrain_height, water_height_m), water_mask);
    vec3 normal = terrain_engine_reference_normal(world_xz, seed, vertical_scale);
    normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), water_mask));
    vec2 material_uv = world_xz * frame.material_params.z;

    vec3 world_position = vec3(in_position.x, display_height * vertical_scale, in_position.z);
    gl_Position = frame.view_projection * vec4(world_position, 1.0);
    frag_world_position = world_position;
    frag_normal = normal;
    frag_material_uv = material_uv;
    frag_height_m = terrain_height;
    frag_water_mask = water_mask;
}
