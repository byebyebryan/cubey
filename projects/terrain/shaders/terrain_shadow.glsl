#ifndef CUBEY_TERRAIN_SHADOW_GLSL
#define CUBEY_TERRAIN_SHADOW_GLSL

float terrain_heightfield_shadow(TerrainSourceGpuParameters source, vec2 surface_xz,
                                 float surface_base_height_m, float vertical_scale,
                                 float footprint_m) {
    vec3 light_direction = normalize(atmosphere.primary_light_direction_intensity.xyz);
    float horizontal_length = length(light_direction.xz);
    if (atmosphere.primary_light_direction_intensity.w <= 0.000001) {
        return 1.0;
    }
    if (light_direction.y <= 0.0) {
        return 0.0;
    }
    if (horizontal_length <= 0.000001) {
        return 1.0;
    }

    vec2 horizontal_direction = light_direction.xz / horizontal_length;
    float light_slope = light_direction.y / horizontal_length;
    float origin_height = surface_base_height_m * vertical_scale;
    float distance_m = max(8.0, footprint_m * 2.0);
    float max_horizon_slope = -1e10;
    float height_bias_m = max(1.0, footprint_m * 0.5) * vertical_scale;

    for (int sample_index = 0; sample_index < 16; ++sample_index) {
        float sample_footprint_m = max(footprint_m, distance_m * 0.025);
        float sample_height = terrain_source_base_height(
            source, surface_xz + horizontal_direction * distance_m, sample_footprint_m) *
            vertical_scale;
        max_horizon_slope = max(max_horizon_slope,
            (sample_height - origin_height - height_bias_m) / distance_m);
        distance_m *= 1.6;
    }

    float angular_softness = max(atmosphere.primary_light_color_angular_radius.w * 2.0, 0.008);
    return 1.0 - smoothstep(light_slope - angular_softness,
                            light_slope + angular_softness, max_horizon_slope);
}

#endif // CUBEY_TERRAIN_SHADOW_GLSL
