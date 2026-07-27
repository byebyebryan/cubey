#include "cubey/procedural/noise.glsl"

vec3 planet_surface_cube_face_point(uint face, float u, float v) {
    if (face == 0U) {
        return vec3(1.0, v, -u);
    }
    if (face == 1U) {
        return vec3(-1.0, v, u);
    }
    if (face == 2U) {
        return vec3(u, 1.0, -v);
    }
    if (face == 3U) {
        return vec3(u, -1.0, v);
    }
    if (face == 4U) {
        return vec3(u, v, 1.0);
    }
    return vec3(-u, v, -1.0);
}

float planet_surface_fbm(vec3 p, uint seed, uint octaves) {
    return cubey_proc_fbm_3d(p, seed, octaves);
}

vec2 planet_surface_terrain_detail_strengths() {
    float packed = floor(surface_frame.surface_options.z + 0.5);
    float mid = floor(packed / 4096.0) / 1024.0;
    float fine = mod(packed, 4096.0) / 1024.0;
    return vec2(mid, fine);
}

struct PlanetTerrainFeatureContext {
    vec3 domain_point;
    float continent_mask;
    float mountain_belt;
    float valley_network;
    float relief_gate;
    float plain_gate;
    float land_mask;
};

struct PlanetSurfaceTerrainBands {
    float base_shape_m;
    float broad_relief_m;
    float mid_detail_m;
    float fine_detail_m;
};

float planet_surface_terrain_band_total_m(PlanetSurfaceTerrainBands bands) {
    return bands.base_shape_m + bands.broad_relief_m + bands.mid_detail_m + bands.fine_detail_m;
}

vec3 planet_surface_terrain_domain_point(vec3 sphere_normal) {
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    vec3 base = sphere_normal * max(surface_frame.terrain_options.y, 0.0001);
    vec3 warp = vec3(
        planet_surface_fbm(base * 0.71 + vec3(-2.8, 5.2, 1.1), seed + 271U, 3U),
        planet_surface_fbm(base * 0.67 + vec3(4.6, -1.7, 3.5), seed + 283U, 3U),
        planet_surface_fbm(base * 0.74 + vec3(1.9, 2.4, -6.3), seed + 307U, 3U));
    return base + warp * 0.22;
}

float planet_surface_terrain_ridge_profile(float value, float sharpness) {
    return pow(max(1.0 - abs(value), 0.0), sharpness);
}

float planet_surface_terrain_continent_mask(vec3 sphere_normal) {
    vec3 p = planet_surface_terrain_domain_point(sphere_normal);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float continent = planet_surface_fbm(p * 0.52 + vec3(2.3, -1.7, 4.1), seed + 211U, 5U);
    float breakup = planet_surface_fbm(p * 1.48 + vec3(-3.8, 5.0, 0.9), seed + 547U, 4U);
    float shelf = planet_surface_fbm(p * 3.05 + vec3(5.1, 2.8, -1.6), seed + 659U, 3U);
    float shape = continent * 0.78 + breakup * 0.25 + shelf * 0.08;
    return cubey_proc_smootherstep01((shape + 0.18) / 0.46);
}

float planet_surface_terrain_mountain_belt(vec3 sphere_normal) {
    vec3 p = planet_surface_terrain_domain_point(sphere_normal);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float belt = planet_surface_fbm(p * 1.08 + vec3(-6.5, 1.2, 3.7), seed + 811U, 5U);
    float fold = planet_surface_fbm(p * 2.35 + vec3(3.2, 6.4, -5.7), seed + 919U, 4U);
    return cubey_proc_smootherstep01((belt * 0.72 + fold * 0.24 + 0.08) / 0.44);
}

float planet_surface_terrain_valley_network(vec3 sphere_normal) {
    vec3 p = planet_surface_terrain_domain_point(sphere_normal);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float primary = planet_surface_fbm(p * 2.55 + vec3(6.8, -4.1, 2.3), seed + 1223U, 4U);
    float secondary =
        planet_surface_fbm(p * 5.10 + vec3(-1.2, 8.4, -5.6), seed + 1291U, 3U);
    float channel = primary * 0.78 + secondary * 0.22;
    return planet_surface_terrain_ridge_profile(channel * 1.35, 2.9);
}

PlanetTerrainFeatureContext planet_surface_terrain_feature_context(vec3 sphere_normal) {
    if (surface_frame.terrain_options.x <= 0.0) {
        return PlanetTerrainFeatureContext(vec3(0.0), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    }
    float continent_mask = planet_surface_terrain_continent_mask(sphere_normal);
    float mountain_belt = planet_surface_terrain_mountain_belt(sphere_normal);
    float relief_gate = cubey_proc_smootherstep01((continent_mask - 0.24) / 0.54);
    float plain_gate = cubey_proc_smootherstep01((continent_mask - 0.36) / 0.42) *
                       (1.0 - smoothstep(0.30, 0.72, mountain_belt));
    return PlanetTerrainFeatureContext(
        planet_surface_terrain_domain_point(sphere_normal),
        continent_mask,
        mountain_belt,
        planet_surface_terrain_valley_network(sphere_normal),
        relief_gate,
        plain_gate,
        cubey_proc_smootherstep01((continent_mask - 0.30) / 0.42));
}

PlanetSurfaceTerrainBands planet_surface_terrain_bands(vec3 sphere_normal) {
    float height_scale = surface_frame.terrain_options.x;
    if (height_scale <= 0.0) {
        return PlanetSurfaceTerrainBands(0.0, 0.0, 0.0, 0.0);
    }

    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    PlanetTerrainFeatureContext features = planet_surface_terrain_feature_context(sphere_normal);
    vec3 p = features.domain_point;
    vec2 detail_strength = planet_surface_terrain_detail_strengths();
    float continent_mask = features.continent_mask;
    float mountain_belt = features.mountain_belt;
    float broad = planet_surface_fbm(p + vec3(1.7, -3.2, 5.1), seed, 4U);
    float lowland = planet_surface_fbm(p * 2.15 + vec3(0.4, 3.2, -2.0), seed + 19U, 4U);
    float ridge_source =
        planet_surface_fbm(p * 4.10 + vec3(-4.0, 2.4, 8.5), seed + 37U, 5U);
    float ridge_source_secondary =
        planet_surface_fbm(p * 7.20 + vec3(2.1, -8.2, 4.7), seed + 41U, 4U);
    float ridges = max(planet_surface_terrain_ridge_profile(ridge_source, 2.7),
                       planet_surface_terrain_ridge_profile(ridge_source_secondary, 2.3) * 0.52);
    float basin = planet_surface_fbm(p * 1.18 + vec3(5.7, 0.3, -6.1), seed + 73U, 4U);
    float valleys = features.valley_network;
    float shelf = cubey_proc_smootherstep01((continent_mask - 0.05) / 0.46);
    float relief_gate = features.relief_gate;
    float plain_gate = features.plain_gate;
    float ocean_floor_base = mix(-0.82, -0.12, shelf);
    float ocean_floor_relief = mix(broad * 0.10 + basin * 0.10,
                                   broad * 0.13 + basin * 0.07, shelf);
    float land_base_shape = (continent_mask - 0.34) * 0.78;
    float land_base_relief =
        broad * 0.16 + lowland * 0.20 + (mountain_belt - 0.42) * relief_gate * 0.10;
    float mountains = ridges * mountain_belt * relief_gate * detail_strength.x * 1.36;
    float valley_cut = valleys * relief_gate * detail_strength.x *
                       (0.10 + mountain_belt * 0.26 + plain_gate * 0.08);
    float fine = planet_surface_fbm(p * max(surface_frame.surface_options.w, 0.0001) +
                                        vec3(6.3, 1.1, -7.4),
                                    seed + 113U, 3U) *
                 detail_strength.y * (0.12 + relief_gate * 0.88) *
                 (0.45 + mountain_belt * 0.55);
    PlanetSurfaceTerrainBands bands = PlanetSurfaceTerrainBands(
        mix(ocean_floor_base, land_base_shape, continent_mask) * height_scale,
        mix(ocean_floor_relief, land_base_relief, continent_mask) * height_scale,
        (mountains - valley_cut) * height_scale,
        fine * 0.26 * height_scale);
    float raw_height = planet_surface_terrain_band_total_m(bands);
    float clamped_height = clamp(raw_height, -height_scale, height_scale);
    bands.fine_detail_m += clamped_height - raw_height;
    return bands;
}

float planet_surface_terrain_height_m(vec3 sphere_normal) {
    return planet_surface_terrain_band_total_m(planet_surface_terrain_bands(sphere_normal));
}

vec3 planet_surface_terrain_world_position(uint face, float u, float v) {
    vec3 sphere_normal = normalize(planet_surface_cube_face_point(face, u, v));
    float radius = surface_frame.render_origin_radius.w + planet_surface_terrain_height_m(sphere_normal);
    return sphere_normal * radius;
}

vec3 planet_surface_terrain_normal(uint face, float u, float v, uint patch_level,
                                   vec3 sphere_normal) {
    if (surface_frame.terrain_options.x <= 0.0) {
        return sphere_normal;
    }

    float divisions = patches_per_face_option() * exp2(float(patch_level));
    float patch_width_uv = 2.0 / max(divisions, 1.0);
    float cell_width_uv = patch_width_uv / patch_resolution_option();
    float normal_step = clamp(cell_width_uv * 0.5, 0.00005, 0.02);
    float u0 = clamp(u - normal_step, -1.0, 1.0);
    float u1 = clamp(u + normal_step, -1.0, 1.0);
    float v0 = clamp(v - normal_step, -1.0, 1.0);
    float v1 = clamp(v + normal_step, -1.0, 1.0);
    vec3 tangent_u = planet_surface_terrain_world_position(face, u1, v) -
                     planet_surface_terrain_world_position(face, u0, v);
    vec3 tangent_v = planet_surface_terrain_world_position(face, u, v1) -
                     planet_surface_terrain_world_position(face, u, v0);
    vec3 normal = cross(tangent_u, tangent_v);
    if (length(normal) <= 0.0000001) {
        return sphere_normal;
    }
    normal = normalize(normal);
    if (dot(normal, sphere_normal) < 0.0) {
        normal = -normal;
    }
    return normalize(normal);
}

float planet_surface_normalized_elevation(float height_m) {
    float height_scale = max(surface_frame.terrain_options.x, 1.0);
    return clamp(height_m / height_scale, -1.0, 1.0);
}

float planet_surface_height_above_sea_m(float height_m) {
    return height_m - surface_frame.field_options.x;
}

float planet_surface_water_depth_m(float height_m) {
    return max(-planet_surface_height_above_sea_m(height_m), 0.0);
}

float planet_surface_normalized_bathymetry(float height_m) {
    float depth_scale = max(surface_frame.field_options.y, 0.0001);
    return clamp(planet_surface_water_depth_m(height_m) / depth_scale, 0.0, 1.0);
}

float planet_surface_shoreline_mask(float height_m) {
    float shoreline_width = max(surface_frame.field_options.z, 0.0001);
    return 1.0 - clamp(abs(planet_surface_height_above_sea_m(height_m)) / shoreline_width, 0.0,
                       1.0);
}

float planet_surface_normalized_slope(vec3 sphere_normal, vec3 normal) {
    float dot_up = clamp(dot(normalize(sphere_normal), normalize(normal)), 0.0, 1.0);
    return clamp(acos(dot_up) * 0.6366197723675813, 0.0, 1.0);
}

float planet_surface_land_mask(float height_above_sea_m) {
    float height_scale = max(surface_frame.terrain_options.x, 1.0);
    return cubey_proc_smootherstep01((height_above_sea_m / height_scale + 0.04) / 0.13);
}

float planet_surface_temperature(vec3 sphere_normal, float normalized_elevation) {
    vec3 p = planet_surface_terrain_domain_point(sphere_normal);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float latitude = abs(sphere_normal.y);
    float weather = planet_surface_fbm(p * 1.35 + vec3(8.1, -2.2, 1.4), seed + 1409U, 3U);
    float highland_cooling = max(normalized_elevation, 0.0) * 0.48;
    return clamp(1.0 - latitude * 1.12 - highland_cooling + weather * 0.12,
                 0.0, 1.0);
}

float planet_surface_moisture(vec3 sphere_normal, float shoreline_mask,
                              float normalized_elevation) {
    vec3 p = planet_surface_terrain_domain_point(sphere_normal);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float weather = planet_surface_fbm(p * 2.05 + vec3(-1.5, 7.6, -4.2), seed + 1613U, 4U);
    float latitude_rain = 1.0 - abs(sphere_normal.y) * 0.35;
    return clamp((weather * 0.5 + 0.5) * 0.76 + shoreline_mask * 0.24 +
                     latitude_rain * 0.08 - max(normalized_elevation, 0.0) * 0.18,
                 0.0, 1.0);
}

uint planet_surface_material_id(float height_above_sea_m, float water_depth_m,
                                float shoreline_mask, float normalized_elevation,
                                float normalized_slope, float moisture, float temperature) {
    if (height_above_sea_m <= 0.0) {
        return water_depth_m > 1200.0 ? 0U : 1U;
    }
    if (shoreline_mask > 0.32 && normalized_slope < 0.24) {
        return 2U;
    }
    if (normalized_elevation > 0.68 ||
        (normalized_elevation > 0.42 && temperature < 0.30 + moisture * 0.08 &&
         normalized_slope < 0.30)) {
        return 5U;
    }
    if (normalized_elevation > 0.22 || normalized_slope > 0.30) {
        return 4U;
    }
    return 3U;
}

vec3 planet_surface_material_color(uint material, float normalized_elevation,
                                   float normalized_slope, float moisture, float temperature) {
    float elevation = clamp(normalized_elevation, -1.0, 1.0);
    float slope = clamp(normalized_slope, 0.0, 1.0);
    float wet = clamp(moisture, 0.0, 1.0);
    float warm = clamp(temperature, 0.0, 1.0);
    if (material == 0U) {
        return vec3(0.014, 0.052, 0.118);
    }
    if (material == 1U) {
        return vec3(0.038, 0.155, 0.205);
    }
    if (material == 2U) {
        return vec3(0.560, 0.492, 0.315);
    }
    if (material == 3U) {
        float blend = clamp((elevation + 0.08) / 0.32, 0.0, 1.0);
        vec3 dry = vec3(mix(0.205, 0.335, blend), mix(0.225, 0.300, blend),
                        mix(0.125, 0.155, blend));
        vec3 green = vec3(mix(0.060, 0.115, blend), mix(0.185, 0.320, blend),
                          mix(0.100, 0.110, blend));
        float green_mix = clamp(wet * (0.45 + warm * 0.55), 0.0, 1.0);
        return mix(dry, green, green_mix);
    }
    if (material == 4U) {
        float height_blend = clamp((elevation - 0.22) / 0.44, 0.0, 1.0);
        float rock_blend = max(height_blend, slope);
        return vec3(mix(0.215, 0.500, rock_blend), mix(0.210, 0.475, rock_blend),
                    mix(0.185, 0.430, rock_blend));
    }
    return vec3(0.720, 0.745, 0.790);
}

vec3 planet_surface_material_debug_color(uint material) {
    if (material == 0U) {
        return vec3(0.02, 0.08, 0.46);
    }
    if (material == 1U) {
        return vec3(0.05, 0.30, 0.66);
    }
    if (material == 2U) {
        return vec3(0.86, 0.70, 0.34);
    }
    if (material == 3U) {
        return vec3(0.14, 0.62, 0.22);
    }
    if (material == 4U) {
        return vec3(0.62, 0.48, 0.28);
    }
    return vec3(0.88, 0.92, 0.96);
}

float planet_surface_material_roughness(uint material, float normalized_slope, float moisture) {
    float slope = clamp(normalized_slope, 0.0, 1.0);
    float wet = clamp(moisture, 0.0, 1.0);
    if (material == 0U) {
        return 0.25;
    }
    if (material == 1U) {
        return 0.34;
    }
    if (material == 2U) {
        return 0.78;
    }
    if (material == 3U) {
        return clamp(0.74 - wet * 0.18 + slope * 0.10, 0.45, 0.85);
    }
    if (material == 4U) {
        return clamp(0.76 + slope * 0.16, 0.62, 0.95);
    }
    return 0.58;
}

vec3 planet_surface_slope_color(float normalized_slope) {
    float t = clamp(normalized_slope, 0.0, 1.0);
    return vec3(mix(0.08, 0.95, t), mix(0.25, 0.66, t), mix(0.42, 0.14, t));
}
