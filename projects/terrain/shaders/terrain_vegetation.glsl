#ifndef CUBEY_TERRAIN_VEGETATION_GLSL
#define CUBEY_TERRAIN_VEGETATION_GLSL

#include "cubey/procedural/noise.glsl"

struct TerrainVegetationCoverage {
    float ground;
    float woody;
};

float terrain_vegetation_filtered_noise(vec2 position, float scale_m,
                                        float pixel_footprint_m, float fade_start_m,
                                        float fade_end_m, vec2 offset) {
    float value = cubey_proc_value_noise_pcg_2d(position / scale_m + offset);
    float visibility = 1.0 - smoothstep(fade_start_m, fade_end_m, pixel_footprint_m);
    return mix(0.5, value, visibility);
}

TerrainVegetationCoverage terrain_vegetation_coverage(vec3 source_normal, vec2 world_xz,
                                                       float height_m,
                                                       float landform_concavity_m,
                                                       float pixel_footprint_m) {
    const mat2 regional_rotation = mat2(0.91, -0.41, 0.41, 0.91);
    const mat2 patch_rotation = mat2(0.72, 0.69, -0.69, 0.72);
    float regional = cubey_proc_value_noise_pcg_2d(
        regional_rotation * world_xz / 1500.0 + vec2(17.0, -23.0));
    float patch_noise = terrain_vegetation_filtered_noise(
        patch_rotation * world_xz, 155.0, pixel_footprint_m, 80.0, 230.0,
        vec2(-31.0, 11.0));
    float clump = terrain_vegetation_filtered_noise(
        world_xz, 31.0, pixel_footprint_m, 10.0, 55.0, vec2(43.0, 29.0));
    float density = regional * 0.58 + patch_noise * 0.30 + clump * 0.12;

    float upward = clamp(source_normal.y, 0.0, 1.0);
    float lowland = 1.0 - smoothstep(850.0, 1550.0, height_m);
    float ground_suitability = smoothstep(0.68, 0.94, upward) *
        (1.0 - smoothstep(1250.0, 1850.0, height_m));
    float sheltered = smoothstep(-28.0, 70.0, landform_concavity_m);
    float woody_suitability = smoothstep(0.70, 0.94, upward) * lowland *
        mix(0.62, 1.0, sheltered);

    TerrainVegetationCoverage coverage;
    coverage.ground = ground_suitability * smoothstep(0.27, 0.63, density);
    coverage.woody = woody_suitability * smoothstep(0.38, 0.62, density);
    coverage.ground = clamp(coverage.ground, 0.0, 1.0);
    coverage.woody = clamp(coverage.woody, 0.0, 1.0);
    return coverage;
}

vec3 terrain_vegetation_color(TerrainVegetationCoverage coverage) {
    vec3 meadow = cubey_srgb_to_linear(vec3(0.20, 0.27, 0.105));
    vec3 woodland = cubey_srgb_to_linear(vec3(0.050, 0.090, 0.032));
    return mix(meadow, woodland, smoothstep(0.12, 0.72, coverage.woody));
}

float terrain_vegetation_influence(TerrainVegetationCoverage coverage) {
    return min(0.75, coverage.ground * 0.46 + coverage.woody * 0.85);
}

float terrain_vegetation_relief(vec2 world_xz, float pixel_footprint_m,
                                TerrainVegetationCoverage coverage) {
    float visibility = 1.0 - smoothstep(2.5, 18.0, pixel_footprint_m);
    float noise = cubey_proc_value_noise_pcg_2d(world_xz * 0.19 + vec2(9.0, -47.0)) - 0.5;
    return noise * visibility * (coverage.ground * 0.08 + coverage.woody * 0.16);
}

#endif // CUBEY_TERRAIN_VEGETATION_GLSL
