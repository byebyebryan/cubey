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

float planet_surface_terrain_continent_mask(vec3 sphere_normal) {
    vec3 p = sphere_normal * max(surface_frame.terrain_options.y, 0.0001);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float continent = planet_surface_fbm(p * 0.68 + vec3(2.3, -1.7, 4.1), seed + 211U, 5U);
    float breakup = planet_surface_fbm(p * 1.55 + vec3(-3.8, 5.0, 0.9), seed + 547U, 4U);
    return planet_surface_smootherstep((continent + breakup * 0.32 + 0.18 + 0.20) / 0.54);
}

float planet_surface_terrain_mountain_belt(vec3 sphere_normal) {
    vec3 p = sphere_normal * max(surface_frame.terrain_options.y, 0.0001);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float belt = planet_surface_fbm(p * 1.18 + vec3(-6.5, 1.2, 3.7), seed + 811U, 5U);
    return planet_surface_smootherstep((belt + 0.18) / 0.62);
}

float planet_surface_terrain_height_m(vec3 sphere_normal) {
    float height_scale = surface_frame.terrain_options.x;
    if (height_scale <= 0.0) {
        return 0.0;
    }

    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    vec3 p = sphere_normal * max(surface_frame.terrain_options.y, 0.0001);
    vec2 detail_strength = planet_surface_terrain_detail_strengths();
    float continent_mask = planet_surface_terrain_continent_mask(sphere_normal);
    float mountain_belt = planet_surface_terrain_mountain_belt(sphere_normal);
    float broad = planet_surface_fbm(p + vec3(1.7, -3.2, 5.1), seed, 4U);
    float lowland = planet_surface_fbm(p * 2.15 + vec3(0.4, 3.2, -2.0), seed + 19U, 4U);
    float ridge_source =
        planet_surface_fbm(p * 3.35 + vec3(-4.0, 2.4, 8.5), seed + 37U, 5U);
    float ridges = pow(max(1.0 - abs(ridge_source), 0.0), 2.2);
    float ocean_floor =
        -0.54 + broad * 0.10 +
        planet_surface_fbm(p * 1.25 + vec3(5.7, 0.3, -6.1), seed + 73U, 3U) * 0.08;
    float land_base = (continent_mask - 0.46) * 0.70 + lowland * 0.13;
    float mountains = ridges * mountain_belt * continent_mask * detail_strength.x * 0.88;
    float fine = planet_surface_fbm(p * max(surface_frame.surface_options.w, 0.0001) +
                                        vec3(6.3, 1.1, -7.4),
                                    seed + 113U, 3U) *
                 detail_strength.y * (0.22 + continent_mask * 0.78);
    float height =
        (mix(ocean_floor, land_base, continent_mask) + mountains + fine * 0.34) * height_scale;
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
    vec3 p = sphere_normal * max(surface_frame.terrain_options.y, 0.0001);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float latitude = abs(sphere_normal.y);
    float weather = planet_surface_fbm(p * 1.35 + vec3(8.1, -2.2, 1.4), seed + 1409U, 3U);
    return clamp(1.0 - latitude * 1.18 - max(normalized_elevation, 0.0) * 0.45 +
                     weather * 0.10,
                 0.0, 1.0);
}

float planet_surface_moisture(vec3 sphere_normal, float shoreline_mask,
                              float normalized_elevation) {
    vec3 p = sphere_normal * max(surface_frame.terrain_options.y, 0.0001);
    uint seed = uint(surface_frame.terrain_options.z + 0.5);
    float weather = planet_surface_fbm(p * 2.05 + vec3(-1.5, 7.6, -4.2), seed + 1613U, 4U);
    return clamp((weather * 0.5 + 0.5) * 0.82 + shoreline_mask * 0.22 -
                     max(normalized_elevation, 0.0) * 0.16,
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
        return vec3(0.018, 0.070, 0.165);
    }
    if (material == 1U) {
        return vec3(0.040, 0.180, 0.260);
    }
    if (material == 2U) {
        return vec3(0.520, 0.455, 0.285);
    }
    if (material == 3U) {
        float blend = clamp((elevation + 0.08) / 0.32, 0.0, 1.0);
        vec3 dry = vec3(mix(0.225, 0.340, blend), mix(0.245, 0.300, blend),
                        mix(0.135, 0.145, blend));
        vec3 green = vec3(mix(0.070, 0.120, blend), mix(0.220, 0.360, blend),
                          mix(0.115, 0.105, blend));
        float green_mix = clamp(wet * (0.45 + warm * 0.55), 0.0, 1.0);
        return mix(dry, green, green_mix);
    }
    if (material == 4U) {
        float height_blend = clamp((elevation - 0.22) / 0.44, 0.0, 1.0);
        float rock_blend = max(height_blend, slope);
        return vec3(mix(0.205, 0.515, rock_blend), mix(0.205, 0.470, rock_blend),
                    mix(0.185, 0.400, rock_blend));
    }
    return vec3(0.680, 0.720, 0.780);
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

vec3 planet_surface_slope_color(float normalized_slope) {
    float t = clamp(normalized_slope, 0.0, 1.0);
    return vec3(mix(0.08, 0.95, t), mix(0.25, 0.66, t), mix(0.42, 0.14, t));
}
