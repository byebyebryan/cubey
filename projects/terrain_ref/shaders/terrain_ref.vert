#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_engine_reference.glsl"
#include "shadertoy_mountain_reference.glsl"

layout(push_constant) uniform TerrainRefPushConstants {
    mat4 view_projection;
    vec4 light_direction_extent;
    vec4 terrain_params;
    vec4 water_params;
    vec4 camera_position_fog;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_material_uv;
layout(location = 3) out float frag_height_m;
layout(location = 4) out float frag_water_mask;

bool terrain_ref_uses_shadertoy_mountain() {
    return pc.water_params.w > 0.5;
}

float terrain_ref_height(vec2 world_xz, vec2 seed, bool surface_detail) {
    if (terrain_ref_uses_shadertoy_mountain()) {
        return shadertoy_mountain_reference_height(world_xz, seed, surface_detail);
    }
    return terrain_engine_reference_height(world_xz, seed);
}

vec3 terrain_ref_normal(vec2 world_xz, vec2 seed, float vertical_scale) {
    if (terrain_ref_uses_shadertoy_mountain()) {
        return shadertoy_mountain_reference_normal(world_xz, seed, vertical_scale);
    }
    return terrain_engine_reference_normal(world_xz, seed, vertical_scale);
}

float terrain_ref_material_uv_scale() {
    return terrain_ref_uses_shadertoy_mountain() ? 0.0042 : 0.006;
}

void main() {
    vec2 seed = pc.terrain_params.xy;
    float vertical_scale = pc.terrain_params.z;
    float water_enabled = pc.terrain_params.w;
    float water_height_m = pc.water_params.x;
    vec2 world_xz = in_position.xz;
    float terrain_height = terrain_ref_height(world_xz, seed, false);
    float water_mask = water_enabled * step(terrain_height, water_height_m);
    float display_height = mix(terrain_height, max(terrain_height, water_height_m), water_mask);
    vec3 normal = terrain_ref_normal(world_xz, seed, vertical_scale);
    normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), water_mask));
    vec2 material_uv = world_xz * terrain_ref_material_uv_scale();

    vec3 world_position = vec3(in_position.x, display_height * vertical_scale, in_position.z);
    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    frag_world_position = world_position;
    frag_normal = normal;
    frag_material_uv = material_uv;
    frag_height_m = terrain_height;
    frag_water_mask = water_mask;
}
