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

float planet_surface_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float planet_surface_smootherstep(float value) {
    float x = clamp(value, 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

uint planet_surface_hash_u32(ivec3 p, uint seed) {
    uint value = seed;
    value ^= uint(p.x) * 0x9e3779b9U;
    value ^= uint(p.y) * 0x85ebca6bU;
    value ^= uint(p.z) * 0xc2b2ae35U;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

float planet_surface_hash01(ivec3 p, uint seed) {
    return float(planet_surface_hash_u32(p, seed) >> 8U) / 16777215.0;
}

float planet_surface_value_noise(vec3 p, uint seed) {
    ivec3 p0 = ivec3(floor(p));
    vec3 t = vec3(planet_surface_smootherstep(p.x - float(p0.x)),
                  planet_surface_smootherstep(p.y - float(p0.y)),
                  planet_surface_smootherstep(p.z - float(p0.z)));

    float x00 = planet_surface_lerp(
        planet_surface_hash01(p0 + ivec3(0, 0, 0), seed) * 2.0 - 1.0,
        planet_surface_hash01(p0 + ivec3(1, 0, 0), seed) * 2.0 - 1.0, t.x);
    float x10 = planet_surface_lerp(
        planet_surface_hash01(p0 + ivec3(0, 1, 0), seed) * 2.0 - 1.0,
        planet_surface_hash01(p0 + ivec3(1, 1, 0), seed) * 2.0 - 1.0, t.x);
    float x01 = planet_surface_lerp(
        planet_surface_hash01(p0 + ivec3(0, 0, 1), seed) * 2.0 - 1.0,
        planet_surface_hash01(p0 + ivec3(1, 0, 1), seed) * 2.0 - 1.0, t.x);
    float x11 = planet_surface_lerp(
        planet_surface_hash01(p0 + ivec3(0, 1, 1), seed) * 2.0 - 1.0,
        planet_surface_hash01(p0 + ivec3(1, 1, 1), seed) * 2.0 - 1.0, t.x);
    return planet_surface_lerp(planet_surface_lerp(x00, x10, t.y),
                               planet_surface_lerp(x01, x11, t.y), t.z);
}

float planet_surface_fbm(vec3 p, uint seed, uint octaves) {
    float amplitude = 0.5;
    float frequency = 1.0;
    float sum = 0.0;
    float weight = 0.0;
    for (uint octave = 0U; octave < octaves; ++octave) {
        sum += planet_surface_value_noise(p * frequency, seed + octave * 1013U) * amplitude;
        weight += amplitude;
        frequency *= 2.03;
        amplitude *= 0.5;
    }
    return weight > 0.0 ? sum / weight : 0.0;
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
    return planet_surface_smootherstep((shape + 0.18) / 0.46);
}

float planet_surface_terrain_mountain_belt(vec3 sphere_normal) {
    vec3 p = planet_surface_terrain_domain_point(sphere_normal);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float belt = planet_surface_fbm(p * 1.08 + vec3(-6.5, 1.2, 3.7), seed + 811U, 5U);
    float fold = planet_surface_fbm(p * 2.35 + vec3(3.2, 6.4, -5.7), seed + 919U, 4U);
    return planet_surface_smootherstep((belt * 0.72 + fold * 0.24 + 0.08) / 0.44);
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
    float relief_gate = planet_surface_smootherstep((continent_mask - 0.24) / 0.54);
    float plain_gate = planet_surface_smootherstep((continent_mask - 0.36) / 0.42) *
                       (1.0 - smoothstep(0.30, 0.72, mountain_belt));
    return PlanetTerrainFeatureContext(
        planet_surface_terrain_domain_point(sphere_normal),
        continent_mask,
        mountain_belt,
        planet_surface_terrain_valley_network(sphere_normal),
        relief_gate,
        plain_gate,
        planet_surface_smootherstep((continent_mask - 0.30) / 0.42));
}

float planet_surface_terrain_height_m(vec3 sphere_normal) {
    float height_scale = surface_frame.terrain_options.x;
    if (height_scale <= 0.0) {
        return 0.0;
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
    float shelf = planet_surface_smootherstep((continent_mask - 0.05) / 0.46);
    float ocean_floor = mix(-0.72 + broad * 0.08 + basin * 0.07,
                            -0.18 + broad * 0.10 + basin * 0.04, shelf);
    float land_base = (continent_mask - 0.38) * 0.72 + broad * 0.11 + lowland * 0.16;
    float relief_gate = features.relief_gate;
    float plain_gate = features.plain_gate;
    float mountains = ridges * mountain_belt * relief_gate * detail_strength.x * 1.22;
    float valley_cut = valleys * relief_gate * detail_strength.x *
                       (0.08 + mountain_belt * 0.22 + plain_gate * 0.06);
    float fine = planet_surface_fbm(p * max(surface_frame.surface_options.w, 0.0001) +
                                        vec3(6.3, 1.1, -7.4),
                                    seed + 113U, 3U) *
                 detail_strength.y * (0.12 + relief_gate * 0.88) *
                 (0.45 + mountain_belt * 0.55);
    float height =
        (mix(ocean_floor, land_base, continent_mask) + mountains - valley_cut + fine * 0.26) *
        height_scale;
    return clamp(height, -height_scale, height_scale);
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
    return planet_surface_smootherstep((height_above_sea_m / height_scale + 0.04) / 0.13);
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
