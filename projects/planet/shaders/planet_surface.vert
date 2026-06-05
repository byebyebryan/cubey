#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 1) in float in_skirt;
layout(location = 2) in uvec4 in_patch_id;
layout(location = 3) in float in_screen_error_px;

layout(push_constant) uniform PlanetPushConstants {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 render_origin_radius;
    vec4 surface_options;
    vec4 terrain_options;
} pc;

uint packed_patch_lod_option() {
    return uint(pc.surface_options.y + 0.5);
}

float patches_per_face_option() {
    return float(max(packed_patch_lod_option() / 16U, 1U));
}

float max_lod_option() {
    return float(packed_patch_lod_option() & 15U);
}

float wire_overlay_option() {
    return fract(pc.surface_options.x) > 0.1 ? 1.0 : 0.0;
}

int debug_view_option() {
    return int(floor(pc.surface_options.x));
}

vec2 terrain_detail_strengths() {
    float packed = floor(pc.surface_options.z + 0.5);
    float mid = floor(packed / 4096.0) / 1024.0;
    float fine = mod(packed, 4096.0) / 1024.0;
    return vec2(mid, fine);
}

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;

vec3 cube_face_point(uint face, float u, float v) {
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

vec4 patch_bounds() {
    float patches_per_face = patches_per_face_option();
    float level_divisions = exp2(float(in_patch_id.y));
    float divisions = patches_per_face * level_divisions;
    float inv_divisions = 1.0 / divisions;
    float u0 = -1.0 + 2.0 * float(in_patch_id.z) * inv_divisions;
    float v0 = -1.0 + 2.0 * float(in_patch_id.w) * inv_divisions;
    float u1 = -1.0 + 2.0 * float(in_patch_id.z + 1U) * inv_divisions;
    float v1 = -1.0 + 2.0 * float(in_patch_id.w + 1U) * inv_divisions;
    return vec4(u0, v0, u1, v1);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float smootherstep(float value) {
    float x = clamp(value, 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

uint hash_u32(ivec3 p, uint seed) {
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

float hash01(ivec3 p, uint seed) {
    return float(hash_u32(p, seed) >> 8U) / 16777215.0;
}

float value_noise(vec3 p, uint seed) {
    ivec3 p0 = ivec3(floor(p));
    vec3 t = vec3(smootherstep(p.x - float(p0.x)), smootherstep(p.y - float(p0.y)),
                  smootherstep(p.z - float(p0.z)));

    float x00 = lerp(hash01(p0 + ivec3(0, 0, 0), seed) * 2.0 - 1.0,
                     hash01(p0 + ivec3(1, 0, 0), seed) * 2.0 - 1.0, t.x);
    float x10 = lerp(hash01(p0 + ivec3(0, 1, 0), seed) * 2.0 - 1.0,
                     hash01(p0 + ivec3(1, 1, 0), seed) * 2.0 - 1.0, t.x);
    float x01 = lerp(hash01(p0 + ivec3(0, 0, 1), seed) * 2.0 - 1.0,
                     hash01(p0 + ivec3(1, 0, 1), seed) * 2.0 - 1.0, t.x);
    float x11 = lerp(hash01(p0 + ivec3(0, 1, 1), seed) * 2.0 - 1.0,
                     hash01(p0 + ivec3(1, 1, 1), seed) * 2.0 - 1.0, t.x);
    return lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
}

float fbm(vec3 p, uint seed, uint octaves) {
    float amplitude = 0.5;
    float frequency = 1.0;
    float sum = 0.0;
    float weight = 0.0;
    for (uint octave = 0U; octave < octaves; ++octave) {
        sum += value_noise(p * frequency, seed + octave * 1013U) * amplitude;
        weight += amplitude;
        frequency *= 2.03;
        amplitude *= 0.5;
    }
    return weight > 0.0 ? sum / weight : 0.0;
}

float terrain_height_m(vec3 normal) {
    float height_scale = pc.terrain_options.x;
    if (height_scale <= 0.0) {
        return 0.0;
    }

    uint seed = uint(pc.terrain_options.z + 0.5);
    vec3 p = normal * max(pc.terrain_options.y, 0.0001);
    vec2 detail_strength = terrain_detail_strengths();
    float broad = fbm(p + vec3(1.7, -3.2, 5.1), seed, 4U);
    float ridge_source = fbm(p * 2.35 + vec3(-4.0, 2.4, 8.5), seed + 37U, 5U);
    float mid = ((1.0 - abs(ridge_source)) * 2.0 - 1.0) * detail_strength.x;
    float fine =
        fbm(p * max(pc.surface_options.w, 0.0001) + vec3(6.3, 1.1, -7.4), seed + 113U, 3U) *
        detail_strength.y;
    float height = (broad * 0.58 + mid + fine) * height_scale;
    return clamp(height, -height_scale, height_scale);
}

vec3 terrain_world_position(uint face, float u, float v) {
    vec3 sphere_normal = normalize(cube_face_point(face, u, v));
    float radius = pc.render_origin_radius.w + terrain_height_m(sphere_normal);
    return sphere_normal * radius;
}

vec3 terrain_normal(uint face, float u, float v, vec3 base_normal) {
    if (pc.terrain_options.x <= 0.0) {
        return base_normal;
    }

    const float normal_step = 0.0015;
    float u0 = clamp(u - normal_step, -1.0, 1.0);
    float u1 = clamp(u + normal_step, -1.0, 1.0);
    float v0 = clamp(v - normal_step, -1.0, 1.0);
    float v1 = clamp(v + normal_step, -1.0, 1.0);
    vec3 tangent_u = terrain_world_position(face, u1, v) - terrain_world_position(face, u0, v);
    vec3 tangent_v = terrain_world_position(face, u, v1) - terrain_world_position(face, u, v0);
    vec3 normal = cross(tangent_u, tangent_v);
    if (length(normal) <= 0.0000001) {
        return base_normal;
    }
    normal = normalize(normal);
    if (dot(normal, base_normal) < 0.0) {
        normal = -normal;
    }
    return normalize(normal);
}

vec3 face_color(uint face) {
    if (face == 0U) {
        return vec3(0.95, 0.22, 0.18);
    }
    if (face == 1U) {
        return vec3(0.18, 0.45, 0.95);
    }
    if (face == 2U) {
        return vec3(0.20, 0.78, 0.36);
    }
    if (face == 3U) {
        return vec3(0.96, 0.70, 0.18);
    }
    if (face == 4U) {
        return vec3(0.58, 0.30, 0.92);
    }
    return vec3(0.15, 0.78, 0.78);
}

vec3 patch_color() {
    uint hash = in_patch_id.x * 73856093U;
    hash ^= in_patch_id.y * 19349663U;
    hash ^= in_patch_id.z * 83492791U;
    hash ^= in_patch_id.w * 2654435761U;
    float band = float(hash % 97U) / 96.0;
    return vec3(0.18 + 0.58 * band, 0.78 - 0.42 * band, 0.28 + 0.36 * (1.0 - band));
}

vec3 lod_color() {
    float max_lod = max(max_lod_option(), 0.0);
    float t = max_lod <= 0.0 ? 0.0 : float(in_patch_id.y) / max_lod;
    return vec3(0.12 + 0.82 * t, 0.55 - 0.28 * t, 0.95 - 0.76 * t);
}

vec3 screen_error_color() {
    float target = max(pc.light_direction_debug.w, 0.0001);
    float t = clamp(in_screen_error_px / target, 0.0, 2.0) * 0.5;
    return vec3(0.16 + 0.80 * t, 0.82 - 0.46 * t, 0.24);
}

vec3 final_color(vec3 normal, float height_m) {
    float height_scale = pc.terrain_options.x;
    if (height_scale > 0.0) {
        float t = clamp(height_m / max(height_scale, 1.0), -1.0, 1.0);
        if (t < -0.15) {
            return vec3(0.035, 0.105, 0.190);
        }
        if (t < 0.22) {
            float blend = (t + 0.15) / 0.37;
            return vec3(lerp(0.070, 0.120, blend), lerp(0.170, 0.280, blend),
                        lerp(0.130, 0.100, blend));
        }
        if (t < 0.62) {
            float blend = (t - 0.22) / 0.40;
            return vec3(lerp(0.130, 0.360, blend), lerp(0.260, 0.310, blend),
                        lerp(0.110, 0.230, blend));
        }
        return vec3(0.66, 0.70, 0.76);
    }
    float latitude = normal.y * 0.5 + 0.5;
    return vec3(0.035 + 0.030 * latitude, 0.100 + 0.070 * latitude,
                0.230 + 0.200 * latitude);
}

vec3 vertex_color(vec3 normal, float height_m) {
    int debug_view = debug_view_option();
    if (debug_view == 1) {
        return face_color(in_patch_id.x);
    }
    if (debug_view == 2) {
        return patch_color();
    }
    if (debug_view == 3) {
        return lod_color();
    }
    if (debug_view == 4) {
        return screen_error_color();
    }
    if (debug_view == 5) {
        if (in_skirt > 0.5) {
            return vec3(1.0, 0.82, 0.22);
        }
        vec3 color = final_color(normal, height_m);
        return vec3(color.r * 0.28, color.g * 0.34, color.b * 0.42);
    }
    return final_color(normal, height_m);
}

void main() {
    vec4 bounds = patch_bounds();
    float u = mix(bounds.x, bounds.z, in_uv.x);
    float v = mix(bounds.y, bounds.w, in_uv.y);
    vec3 sphere_normal = normalize(cube_face_point(in_patch_id.x, u, v));
    float height_m = terrain_height_m(sphere_normal);
    vec3 normal = terrain_normal(in_patch_id.x, u, v, sphere_normal);
    vec3 world_position = sphere_normal * (pc.render_origin_radius.w + height_m);
    world_position -= normal * pc.terrain_options.w * in_skirt;
    vec3 render_position = world_position - pc.render_origin_radius.xyz;

    out_color = vertex_color(normal, height_m);
    out_normal = normal;
    out_uv = in_uv;
    gl_Position = pc.view_projection * vec4(render_position, 1.0);
}
