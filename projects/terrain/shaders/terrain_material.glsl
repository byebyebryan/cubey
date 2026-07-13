#ifndef CUBEY_TERRAIN_MATERIAL_GLSL
#define CUBEY_TERRAIN_MATERIAL_GLSL

#include "cubey/color_space.glsl"
#include "cubey/procedural/noise.glsl"
#include "terrain_vegetation.glsl"

struct TerrainMaterialSample {
    vec3 base_color;
    float roughness;
    vec3 detail_normal;
    vec4 material_weights;
    float cavity;
    float blend_height;
    TerrainVegetationCoverage vegetation;
};

float terrain_material_scale_visibility(float scale_m, float pixel_footprint_m) {
    return 1.0 - smoothstep(scale_m * 0.24, scale_m * 0.52, pixel_footprint_m);
}

float terrain_material_field(vec3 world_position, float scale_m, uint seed,
                             float pixel_footprint_m) {
    float value = cubey_proc_value_noise_3d(world_position / scale_m, seed);
    return mix(0.0, value, terrain_material_scale_visibility(scale_m, pixel_footprint_m));
}

vec3 terrain_material_fields(vec3 world_position, uint seed, float pixel_footprint_m) {
    const mat2 rotation = mat2(0.8, -0.6, 0.6, 0.8);
    vec2 rotated_xz = rotation * world_position.xz;
    vec3 sample_position = vec3(rotated_xz.x, world_position.y * 0.72, rotated_xz.y);
    return vec3(
        terrain_material_field(sample_position, 680.0, seed + 0x6d2b79f5U,
                               pixel_footprint_m),
        terrain_material_field(sample_position + vec3(91.0, -37.0, 53.0), 145.0,
                               seed + 0x1b873593U, pixel_footprint_m),
        terrain_material_field(sample_position + vec3(-17.0, 29.0, 71.0), 34.0,
                               seed + 0x85ebca6bU, pixel_footprint_m));
}

float terrain_material_relief(vec3 world_position, uint seed, float pixel_footprint_m,
                              TerrainVegetationCoverage vegetation) {
    vec3 fields = terrain_material_fields(world_position, seed, pixel_footprint_m);
    return fields.y * 15.0 + fields.z * 2.5 +
        terrain_vegetation_relief(world_position.xz, pixel_footprint_m, vegetation);
}

vec3 terrain_material_detail_normal(vec3 source_normal, vec3 world_position, uint seed,
                                    float pixel_footprint_m,
                                    TerrainVegetationCoverage vegetation) {
    const float step_m = 1.0;
    float center = terrain_material_relief(
        world_position, seed, pixel_footprint_m, vegetation);
    vec2 gradient = vec2(
        terrain_material_relief(world_position + vec3(step_m, 0.0, 0.0), seed,
                                pixel_footprint_m, vegetation) - center,
        terrain_material_relief(world_position + vec3(0.0, 0.0, step_m), seed,
                                pixel_footprint_m, vegetation) - center) / step_m;
    if (any(isnan(gradient)) || any(isinf(gradient))) {
        return source_normal;
    }
    vec3 normal = vec3(source_normal.x - gradient.x, source_normal.y,
                       source_normal.z - gradient.y);
    float length_squared = dot(normal, normal);
    return length_squared > 1e-10 ? normal * inversesqrt(length_squared) : source_normal;
}

TerrainMaterialSample terrain_material_sample(vec3 source_normal, vec3 world_position,
                                              float height_m,
                                              float landform_concavity_m,
                                              float pixel_footprint_m,
                                              bool backdrop_presentation,
                                              vec4 source_elevation, uint source_seed) {
    float upward = clamp(source_normal.y, 0.0, 1.0);
    float slope = 1.0 - upward;
    float relief_height = max(source_elevation.y, 1.0);
    float normalized_height = clamp((height_m - source_elevation.x) / relief_height, 0.0, 1.0);
    float mountain_factor = smoothstep(1300.0, 2800.0, relief_height);
    float upland_factor = smoothstep(300.0, 1100.0, relief_height) * (1.0 - mountain_factor);
    vec3 fields = terrain_material_fields(world_position, source_seed, pixel_footprint_m);
    float macro = fields.x * 0.5 + 0.5;
    float meso = fields.y * 0.5 + 0.5;
    float local = fields.z * 0.5 + 0.5;

    float exposed_rock = smoothstep(0.17, 0.54, slope);
    float alpine_rock = mountain_factor * smoothstep(0.42, 0.72, normalized_height) *
        smoothstep(0.035, 0.30, slope) * mix(0.68, 1.0, meso);
    float rock_weight = max(exposed_rock, alpine_rock);

    float moderate_slope = smoothstep(0.08, 0.27, slope) *
        (1.0 - smoothstep(0.48, 0.68, slope));
    float sheltered = smoothstep(-30.0, 70.0, landform_concavity_m);
    float scree_weight = moderate_slope * mix(0.30, 0.92, sheltered) *
        mix(0.62, 1.0, meso) * max(mountain_factor, upland_factor * 0.72);

    float snowline = mix(0.68, 0.27, mountain_factor) + (macro - 0.5) * 0.12 +
        (meso - 0.5) * 0.055;
    float snow_weight = mountain_factor *
        smoothstep(snowline - 0.045, snowline + 0.075, normalized_height) *
        smoothstep(0.30, 0.82, upward) * mix(0.72, 1.0, meso);

    snow_weight = clamp(snow_weight, 0.0, 1.0);
    rock_weight = clamp(rock_weight * (1.0 - snow_weight), 0.0, 1.0);
    scree_weight = clamp(scree_weight * (1.0 - rock_weight) * (1.0 - snow_weight), 0.0, 1.0);
    float ground_weight = max(0.0, 1.0 - snow_weight - rock_weight - scree_weight);
    vec4 material_weights = vec4(ground_weight, scree_weight, rock_weight, snow_weight);
    material_weights /= max(dot(material_weights, vec4(1.0)), 0.0001);

    vec3 ground = cubey_srgb_to_linear(vec3(0.27, 0.255, 0.205));
    vec3 scree = cubey_srgb_to_linear(vec3(0.35, 0.315, 0.26));
    vec3 rock = cubey_srgb_to_linear(vec3(0.39, 0.385, 0.37));
    vec3 snow = cubey_srgb_to_linear(vec3(0.82, 0.845, 0.86));

    ground *= mix(0.74, 1.17, macro) * mix(0.88, 1.10, meso);
    scree *= mix(0.74, 1.20, macro) * mix(0.82, 1.16, meso) *
        mix(0.92, 1.08, local);
    float strata = 0.5 + 0.5 * sin((height_m + fields.x * 95.0 + fields.y * 32.0) / 58.0);
    rock *= mix(0.70, 1.22, macro) * mix(0.66, 1.30, meso) *
        mix(0.86, 1.14, local) * mix(0.76, 1.20, strata);
    snow *= mix(0.96, 1.035, meso);

    vec3 base_color = ground * material_weights.x + scree * material_weights.y +
        rock * material_weights.z + snow * material_weights.w;
    float roughness = dot(material_weights, vec4(0.94, 0.91, 0.77, 0.84));

    TerrainVegetationCoverage vegetation = backdrop_presentation
        ? terrain_vegetation_coverage(source_normal, world_position.xz, height_m,
                                      landform_concavity_m, pixel_footprint_m)
        : TerrainVegetationCoverage(0.0, 0.0);
    float vegetation_influence = terrain_vegetation_influence(vegetation) *
        material_weights.x;
    base_color = mix(base_color, terrain_vegetation_color(vegetation), vegetation_influence);
    roughness = mix(roughness, 0.95, vegetation_influence);

    TerrainMaterialSample material;
    material.base_color = max(base_color, vec3(0.0));
    material.roughness = clamp(roughness, 0.04, 1.0);
    material.detail_normal = terrain_material_detail_normal(
        source_normal, world_position, source_seed, pixel_footprint_m, vegetation);
    material.material_weights = material_weights;
    material.cavity = 1.0;
    material.blend_height = 0.5;
    material.vegetation = vegetation;
    return material;
}

TerrainMaterialSample terrain_clay_material(vec3 source_normal) {
    TerrainMaterialSample material;
    material.base_color = cubey_srgb_to_linear(vec3(0.38, 0.39, 0.40));
    material.roughness = 0.90;
    material.detail_normal = source_normal;
    material.material_weights = vec4(0.0);
    material.cavity = 1.0;
    material.blend_height = 0.5;
    material.vegetation = TerrainVegetationCoverage(0.0, 0.0);
    return material;
}

#endif // CUBEY_TERRAIN_MATERIAL_GLSL
