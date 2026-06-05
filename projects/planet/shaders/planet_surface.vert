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
    float patches_per_face = max(pc.surface_options.z, 1.0);
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
    float max_lod = max(pc.surface_options.w, 0.0);
    float t = max_lod <= 0.0 ? 0.0 : float(in_patch_id.y) / max_lod;
    return vec3(0.12 + 0.82 * t, 0.55 - 0.28 * t, 0.95 - 0.76 * t);
}

vec3 screen_error_color() {
    float target = max(pc.light_direction_debug.w, 0.0001);
    float t = clamp(in_screen_error_px / target, 0.0, 2.0) * 0.5;
    return vec3(0.16 + 0.80 * t, 0.82 - 0.46 * t, 0.24);
}

vec3 final_color(vec3 normal) {
    float latitude = normal.y * 0.5 + 0.5;
    return vec3(0.035 + 0.030 * latitude, 0.100 + 0.070 * latitude,
                0.230 + 0.200 * latitude);
}

vec3 vertex_color(vec3 normal) {
    int debug_view = int(pc.surface_options.y + 0.5);
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
        vec3 color = final_color(normal);
        return vec3(color.r * 0.28, color.g * 0.34, color.b * 0.42);
    }
    return final_color(normal);
}

void main() {
    vec4 bounds = patch_bounds();
    float u = mix(bounds.x, bounds.z, in_uv.x);
    float v = mix(bounds.y, bounds.w, in_uv.y);
    vec3 sphere_normal = normalize(cube_face_point(in_patch_id.x, u, v));
    float radius = pc.render_origin_radius.w - pc.terrain_options.w * in_skirt;
    vec3 world_position = sphere_normal * radius;
    vec3 render_position = world_position - pc.render_origin_radius.xyz;

    out_color = vertex_color(sphere_normal);
    out_normal = sphere_normal;
    out_uv = in_uv;
    gl_Position = pc.view_projection * vec4(render_position, 1.0);
}
