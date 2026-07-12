#ifndef CUBEY_TERRAIN_MATERIAL_GLSL
#define CUBEY_TERRAIN_MATERIAL_GLSL

#include "cubey/color_space.glsl"
#include "cubey/procedural/noise.glsl"
#include "terrain_vegetation.glsl"

struct TerrainMaterialSample {
    vec3 base_color;
    float roughness;
    vec3 detail_normal;
    TerrainVegetationCoverage vegetation;
};

float terrain_material_broad_variation(vec2 world_xz) {
    return cubey_proc_value_noise_pcg_2d(world_xz * 0.0022);
}

float terrain_material_color_variation(vec2 world_xz, float pixel_footprint_m) {
    float broad = terrain_material_broad_variation(world_xz);
    float medium_visibility = 1.0 - smoothstep(5.0, 24.0, pixel_footprint_m);
    const mat2 medium_rotation = mat2(0.80, -0.60, 0.60, 0.80);
    float medium = cubey_proc_value_noise_pcg_2d(
        medium_rotation * world_xz * 0.045 + vec2(19.0, -31.0));
    return mix(broad, mix(broad, medium, 0.34), medium_visibility);
}

float terrain_material_relief(vec2 world_xz, float pixel_footprint_m,
                              TerrainVegetationCoverage vegetation) {
    float broad_visibility = 1.0 - smoothstep(7.0, 26.0, pixel_footprint_m);
    float fine_visibility = 1.0 - smoothstep(1.5, 7.0, pixel_footprint_m);
    const mat2 broad_rotation = mat2(0.86, -0.51, 0.51, 0.86);
    const mat2 fine_rotation = mat2(0.64, 0.77, -0.77, 0.64);
    float broad = cubey_proc_value_noise_pcg_2d(
        broad_rotation * world_xz * 0.055 + vec2(-7.0, 13.0)) - 0.5;
    float fine = cubey_proc_value_noise_pcg_2d(
        fine_rotation * world_xz * 0.22 + vec2(41.0, 5.0)) - 0.5;
    return broad * broad_visibility * 0.90 + fine * fine_visibility * 0.14 +
        terrain_vegetation_relief(world_xz, pixel_footprint_m, vegetation);
}

vec3 terrain_material_detail_normal(vec3 source_normal, vec2 world_xz,
                                    float pixel_footprint_m,
                                    TerrainVegetationCoverage vegetation) {
    const float step_m = 0.75;
    float center = terrain_material_relief(world_xz, pixel_footprint_m, vegetation);
    vec2 gradient = vec2(
        terrain_material_relief(world_xz + vec2(step_m, 0.0), pixel_footprint_m, vegetation) - center,
        terrain_material_relief(world_xz + vec2(0.0, step_m), pixel_footprint_m, vegetation) - center) /
        step_m;
    if (any(isnan(gradient)) || any(isinf(gradient))) {
        return source_normal;
    }
    vec3 normal = vec3(source_normal.x - gradient.x, source_normal.y,
                       source_normal.z - gradient.y);
    float length_squared = dot(normal, normal);
    return length_squared > 1e-10
        ? normal * inversesqrt(length_squared)
        : source_normal;
}

TerrainMaterialSample terrain_material_sample(vec3 source_normal, vec2 world_xz,
                                              float height_m,
                                              float landform_concavity_m,
                                              float pixel_footprint_m,
                                              bool backdrop_presentation) {
    float slope = 1.0 - clamp(source_normal.y, 0.0, 1.0);
    float broad_variation = terrain_material_broad_variation(world_xz);
    float color_variation = terrain_material_color_variation(world_xz, pixel_footprint_m);

    vec3 ground = cubey_srgb_to_linear(vec3(0.22, 0.27, 0.15));
    vec3 soil = cubey_srgb_to_linear(vec3(0.32, 0.26, 0.19));
    vec3 rock = cubey_srgb_to_linear(vec3(0.38, 0.39, 0.40));
    vec3 snow = cubey_srgb_to_linear(vec3(0.76, 0.80, 0.84));

    ground *= mix(0.74, 1.17, color_variation);
    soil *= mix(0.78, 1.14, color_variation);
    rock *= mix(0.76, 1.16, color_variation);
    snow *= mix(0.94, 1.04, color_variation);

    float soil_mask = smoothstep(0.12, 0.38, slope);
    float rock_mask = smoothstep(0.30, 0.68, slope);
    float alpine_exposure = smoothstep(900.0, 1750.0, height_m) *
        smoothstep(0.08, 0.40, slope) * 0.56;
    rock_mask = max(rock_mask, alpine_exposure);

    vec3 base_color = mix(ground, soil, soil_mask);
    float roughness = mix(0.92, 0.88, soil_mask);
    base_color = mix(base_color, rock, rock_mask);
    roughness = mix(roughness, 0.74, rock_mask);

    float snowline_m = 1450.0 + (broad_variation - 0.5) * 360.0;
    float snow_mask = smoothstep(snowline_m - 120.0, snowline_m + 220.0, height_m) *
        (1.0 - smoothstep(0.30, 0.70, slope));
    base_color = mix(base_color, snow, snow_mask);
    roughness = mix(roughness, 0.82, snow_mask);

    TerrainVegetationCoverage vegetation = backdrop_presentation
        ? terrain_vegetation_coverage(source_normal, world_xz, height_m,
                                      landform_concavity_m, pixel_footprint_m)
        : TerrainVegetationCoverage(0.0, 0.0);
    float vegetation_influence = terrain_vegetation_influence(vegetation);
    base_color = mix(base_color, terrain_vegetation_color(vegetation), vegetation_influence);
    roughness = mix(roughness, 0.94, vegetation_influence);

    TerrainMaterialSample material;
    material.base_color = max(base_color, vec3(0.0));
    material.roughness = clamp(roughness, 0.04, 1.0);
    material.detail_normal = terrain_material_detail_normal(
        source_normal, world_xz, pixel_footprint_m, vegetation);
    material.vegetation = vegetation;
    return material;
}

TerrainMaterialSample terrain_clay_material(vec3 source_normal) {
    TerrainMaterialSample material;
    material.base_color = cubey_srgb_to_linear(vec3(0.38, 0.39, 0.40));
    material.roughness = 0.90;
    material.detail_normal = source_normal;
    material.vegetation = TerrainVegetationCoverage(0.0, 0.0);
    return material;
}

#endif // CUBEY_TERRAIN_MATERIAL_GLSL
