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
    float packed = floor(pc.surface_options.z + 0.5);
    float mid = floor(packed / 4096.0) / 1024.0;
    float fine = mod(packed, 4096.0) / 1024.0;
    return vec2(mid, fine);
}

float planet_surface_terrain_height_m(vec3 sphere_normal) {
    float height_scale = pc.terrain_options.x;
    if (height_scale <= 0.0) {
        return 0.0;
    }

    uint seed = uint(pc.terrain_options.z + 0.5);
    vec3 p = sphere_normal * max(pc.terrain_options.y, 0.0001);
    vec2 detail_strength = planet_surface_terrain_detail_strengths();
    float broad = planet_surface_fbm(p + vec3(1.7, -3.2, 5.1), seed, 4U);
    float ridge_source =
        planet_surface_fbm(p * 2.35 + vec3(-4.0, 2.4, 8.5), seed + 37U, 5U);
    float mid = ((1.0 - abs(ridge_source)) * 2.0 - 1.0) * detail_strength.x;
    float fine = planet_surface_fbm(p * max(pc.surface_options.w, 0.0001) +
                                        vec3(6.3, 1.1, -7.4),
                                    seed + 113U, 3U) *
                 detail_strength.y;
    float height = (broad * 0.58 + mid + fine) * height_scale;
    return clamp(height, -height_scale, height_scale);
}

vec3 planet_surface_terrain_world_position(uint face, float u, float v) {
    vec3 sphere_normal = normalize(planet_surface_cube_face_point(face, u, v));
    float radius = pc.render_origin_radius.w + planet_surface_terrain_height_m(sphere_normal);
    return sphere_normal * radius;
}

vec3 planet_surface_terrain_normal(uint face, float u, float v, uint patch_level,
                                   vec3 sphere_normal) {
    if (pc.terrain_options.x <= 0.0) {
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
