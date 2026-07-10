#version 450
#extension GL_GOOGLE_include_directive : require

#include "terrain_engine_reference.glsl"
#include "shadertoy_biome_reference.glsl"
#include "shadertoy_erosion_reference.glsl"
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
layout(location = 5) out float frag_erosion_delta_m;

bool terrain_ref_uses_shadertoy_mountain() {
    return abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_MOUNTAIN) < 0.5;
}

bool terrain_ref_uses_shadertoy_erosion() {
    return abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_EROSION_BASE) < 0.5 ||
        abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_EROSION_FILTERED) < 0.5;
}

bool terrain_ref_uses_erosion_process() {
    return abs(fract(pc.terrain_params.w) - TERRAIN_REF_EROSION_PROCESS_OFFSET) < 0.05;
}

float terrain_ref_height(vec2 world_xz, vec2 seed, bool surface_detail) {
    if (terrain_ref_uses_shadertoy_mountain()) {
        return shadertoy_mountain_reference_height(world_xz, seed, surface_detail);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_ALPINE) < 0.5) {
        return shadertoy_alpine_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_DUNES) < 0.5) {
        return shadertoy_dunes_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_LAKE_BASIN) < 0.5) {
        return shadertoy_lake_basin_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_BADLANDS) < 0.5) {
        return shadertoy_badlands_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_COAST_ISLAND) < 0.5) {
        return shadertoy_coast_island_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_PLAINS) < 0.5) {
        return shadertoy_plains_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_GORGE) < 0.5) {
        return shadertoy_gorge_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_GLACIAL_HIGHLAND) < 0.5) {
        return shadertoy_glacial_highland_reference_height(world_xz, seed);
    }
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_CRATER_FIELD) < 0.5) {
        return shadertoy_crater_field_reference_height(world_xz, seed);
    }
    return terrain_engine_reference_height(world_xz, seed);
}

vec3 terrain_ref_normal(vec2 world_xz, vec2 seed, float vertical_scale) {
    if (terrain_ref_uses_shadertoy_mountain()) {
        return shadertoy_mountain_reference_normal(world_xz, seed, vertical_scale);
    }
    if (pc.terrain_params.w > 1.5) {
        return shadertoy_biome_reference_normal(world_xz, seed, vertical_scale,
            pc.terrain_params.w);
    }
    return terrain_engine_reference_normal(world_xz, seed, vertical_scale);
}

float terrain_ref_material_uv_scale() {
    if (abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_DUNES) < 0.5) {
        return 0.010;
    }
    if (pc.terrain_params.w > 1.5) {
        return 0.0048;
    }
    return terrain_ref_uses_shadertoy_mountain() || terrain_ref_uses_shadertoy_erosion()
        ? 0.0042
        : 0.006;
}

ShadertoyErosionHeightSlope terrain_ref_source_height_slope(vec2 world_xz, vec2 seed) {
    const float step_m = 8.0;
    float center = terrain_ref_height(world_xz, seed, false);
    float x0 = terrain_ref_height(world_xz - vec2(step_m, 0.0), seed, false);
    float x1 = terrain_ref_height(world_xz + vec2(step_m, 0.0), seed, false);
    float z0 = terrain_ref_height(world_xz - vec2(0.0, step_m), seed, false);
    float z1 = terrain_ref_height(world_xz + vec2(0.0, step_m), seed, false);
    return ShadertoyErosionHeightSlope(center,
        vec2((x1 - x0) / (2.0 * step_m), (z1 - z0) / (2.0 * step_m)));
}

void main() {
    vec2 seed = pc.terrain_params.xy;
    float vertical_scale = pc.terrain_params.z;
    float water_height_m = pc.water_params.x;
    vec2 world_xz = in_position.xz;
    float terrain_height;
    vec3 normal;
    float erosion_delta_m = 0.0;
    if (terrain_ref_uses_shadertoy_erosion()) {
        ShadertoyErosionReferenceSample erosion =
            shadertoy_erosion_reference_sample(world_xz, seed);
        bool filtered_surface =
            abs(pc.terrain_params.w - TERRAIN_REF_RECIPE_SHADERTOY_EROSION_FILTERED) < 0.5;
        terrain_height = filtered_surface ? erosion.filtered_height_m : erosion.base_height_m;
        vec2 gradient = filtered_surface ? erosion.filtered_gradient : erosion.base_gradient;
        normal = normalize(vec3(-gradient.x * vertical_scale, 1.0,
            -gradient.y * vertical_scale));
        erosion_delta_m = erosion.erosion_delta_m;
    } else if (terrain_ref_uses_erosion_process()) {
        ShadertoyErosionHeightSlope source =
            terrain_ref_source_height_slope(world_xz, seed);
        ShadertoyErosionReferenceSample erosion =
            shadertoy_erosion_filter_sample(world_xz, seed, source, 1.0);
        terrain_height = erosion.filtered_height_m;
        normal = normalize(vec3(-erosion.filtered_gradient.x * vertical_scale, 1.0,
            -erosion.filtered_gradient.y * vertical_scale));
        erosion_delta_m = erosion.erosion_delta_m;
    } else {
        terrain_height = terrain_ref_height(world_xz, seed, false);
        normal = terrain_ref_normal(world_xz, seed, vertical_scale);
    }
    float water_mask = step(terrain_height, water_height_m);
    float display_height = mix(terrain_height, max(terrain_height, water_height_m), water_mask);
    normal = normalize(mix(normal, vec3(0.0, 1.0, 0.0), water_mask));
    vec2 material_uv = world_xz * terrain_ref_material_uv_scale();

    vec3 world_position = vec3(in_position.x, display_height * vertical_scale, in_position.z);
    gl_Position = pc.view_projection * vec4(world_position, 1.0);
    frag_world_position = world_position;
    frag_normal = normal;
    frag_material_uv = material_uv;
    frag_height_m = terrain_height;
    frag_water_mask = water_mask;
    frag_erosion_delta_m = erosion_delta_m;
}
