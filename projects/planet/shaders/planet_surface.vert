#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 in_uv;
layout(location = 1) in float in_skirt;
layout(location = 2) in uvec4 in_patch_id;
layout(location = 3) in float in_screen_error_px;
layout(location = 4) in uint in_edge_transition_mask;

layout(set = 0, binding = 0) uniform PlanetSurfaceFrame {
    mat4 view_projection;
    vec4 light_direction_debug;
    vec4 render_origin_radius;
    vec4 surface_options;
    vec4 terrain_options;
    vec4 field_options;
    vec4 camera_horizon;
    vec4 atmosphere_options;
    vec4 haze_color_direct;
    vec4 celestial_equator_plane;
    vec4 celestial_ecliptic_plane;
    vec4 celestial_moon_orbit_plane;
    vec4 celestial_sun_direction;
    vec4 celestial_moon_direction;
    vec4 camera_world_radius;
    vec4 atmosphere_radius_mode;
    vec4 sun_color_intensity;
} surface_frame;

uint packed_patch_lod_option() {
    return uint(surface_frame.surface_options.y + 0.5);
}

float patches_per_face_option() {
    return float(max((packed_patch_lod_option() / 16U) & 15U, 1U));
}

float patch_resolution_option() {
    return float(max(packed_patch_lod_option() / 256U, 1U));
}

float max_lod_option() {
    return float(packed_patch_lod_option() & 15U);
}

float wire_overlay_option() {
    return fract(surface_frame.surface_options.x) > 0.1 ? 1.0 : 0.0;
}

int debug_view_option() {
    return int(floor(surface_frame.surface_options.x));
}

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec3 out_render_position;
layout(location = 4) out vec3 out_sphere_normal;
layout(location = 5) out vec4 out_surface_field;
layout(location = 6) out vec4 out_climate_field;

#include "planet_surface_field.glsl"

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

bool edge_transition_enabled(uint edge_bit) {
    return (in_edge_transition_mask & (1U << edge_bit)) != 0U;
}

float coarser_edge_coord(float value) {
    float coarse_cells = max(patch_resolution_option() * 0.5, 1.0);
    return clamp(floor(value * coarse_cells + 0.5) / coarse_cells, 0.0, 1.0);
}

vec2 edge_transition_uv(vec2 uv) {
    float edge_epsilon = 0.5 / max(patch_resolution_option(), 1.0);
    vec2 transitioned = uv;
    if (edge_transition_enabled(0U) && uv.x <= edge_epsilon) {
        transitioned.y = coarser_edge_coord(transitioned.y);
    }
    if (edge_transition_enabled(1U) && uv.x >= 1.0 - edge_epsilon) {
        transitioned.y = coarser_edge_coord(transitioned.y);
    }
    if (edge_transition_enabled(2U) && uv.y <= edge_epsilon) {
        transitioned.x = coarser_edge_coord(transitioned.x);
    }
    if (edge_transition_enabled(3U) && uv.y >= 1.0 - edge_epsilon) {
        transitioned.x = coarser_edge_coord(transitioned.x);
    }
    return transitioned;
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
    float target = max(surface_frame.light_direction_debug.w, 0.0001);
    float t = clamp(in_screen_error_px / target, 0.0, 2.0) * 0.5;
    return vec3(0.16 + 0.80 * t, 0.82 - 0.46 * t, 0.24);
}

vec3 lod_transition_color() {
    float target = max(surface_frame.light_direction_debug.w, 0.0001);
    float ratio = in_screen_error_px / target;
    float pressure = 1.0 - clamp(abs(ratio - 1.0) / 0.25, 0.0, 1.0);
    return mix(vec3(0.05, 0.10, 0.26), vec3(0.98, 0.72, 0.18), pressure);
}

vec3 cell_edge_color() {
    float divisions = patches_per_face_option() * exp2(float(in_patch_id.y));
    float patch_width_m = (surface_frame.render_origin_radius.w * 2.0) / max(divisions, 1.0);
    float cell_edge_m = max(patch_width_m / patch_resolution_option(), 1.0);
    float detail = clamp(log2(max(surface_frame.render_origin_radius.w, 1.0) / cell_edge_m) / 16.0, 0.0, 1.0);
    return mix(vec3(0.95, 0.42, 0.14), vec3(0.12, 0.78, 0.95), detail);
}

vec3 terrain_height_color(float height_m) {
    float height_scale = max(surface_frame.terrain_options.x, 1.0);
    float t = clamp(height_m / height_scale, -1.0, 1.0) * 0.5 + 0.5;
    if (t < 0.5) {
        float blend = t * 2.0;
        return mix(vec3(0.04, 0.12, 0.36), vec3(0.08, 0.42, 0.20), blend);
    }
    float blend = (t - 0.5) * 2.0;
    return mix(vec3(0.08, 0.42, 0.20), vec3(0.92, 0.88, 0.74), blend);
}

vec3 bathymetry_color(float normalized_bathymetry) {
    float t = clamp(normalized_bathymetry, 0.0, 1.0);
    return mix(vec3(0.04, 0.28, 0.44), vec3(0.01, 0.06, 0.88), t);
}

vec3 shoreline_color(float shoreline_mask) {
    float t = clamp(shoreline_mask, 0.0, 1.0);
    return mix(vec3(0.03, 0.12, 0.20), vec3(0.95, 0.82, 0.32), t);
}

vec3 land_mask_color(float land_mask) {
    float t = clamp(land_mask, 0.0, 1.0);
    return mix(vec3(0.02, 0.10, 0.30), vec3(0.20, 0.62, 0.14), t);
}

vec3 moisture_color(float moisture) {
    float t = clamp(moisture, 0.0, 1.0);
    return mix(vec3(0.56, 0.42, 0.18), vec3(0.04, 0.46, 0.72), t);
}

vec3 temperature_color(float temperature) {
    float t = clamp(temperature, 0.0, 1.0);
    return mix(vec3(0.08, 0.24, 0.82), vec3(0.95, 0.44, 0.10), t);
}

vec3 roughness_color(float roughness) {
    float t = clamp(roughness, 0.0, 1.0);
    return mix(vec3(0.08, 0.09, 0.12), vec3(0.90, 0.90, 0.96), t);
}

vec3 latitude_color(vec3 normal) {
    float latitude = normal.y * 0.5 + 0.5;
    return vec3(0.035 + 0.030 * latitude, 0.100 + 0.070 * latitude,
                0.230 + 0.200 * latitude);
}

vec3 final_color(vec3 normal, uint material, float normalized_elevation, float normalized_slope,
                 float moisture, float temperature) {
    float height_scale = surface_frame.terrain_options.x;
    if (height_scale > 0.0) {
        return planet_surface_material_color(material, normalized_elevation, normalized_slope,
                                             moisture, temperature);
    }
    return latitude_color(normal);
}

vec3 vertex_color(vec3 sphere_normal, vec3 normal, float height_m, uint material,
                  float normalized_elevation, float normalized_slope, float shoreline_mask,
                  float land_mask, float moisture, float temperature, float roughness) {
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
        return lod_transition_color();
    }
    if (debug_view == 6) {
        if (in_skirt > 0.5) {
            return vec3(1.0, 0.82, 0.22);
        }
        vec3 color = latitude_color(normal);
        return vec3(color.r * 0.28, color.g * 0.34, color.b * 0.42);
    }
    if (debug_view == 7) {
        return cell_edge_color();
    }
    if (debug_view == 8) {
        return terrain_height_color(height_m);
    }
    if (debug_view == 9) {
        return planet_surface_slope_color(normalized_slope);
    }
    if (debug_view == 10) {
        return planet_surface_material_debug_color(material);
    }
    if (debug_view == 11) {
        return bathymetry_color(planet_surface_normalized_bathymetry(height_m));
    }
    if (debug_view == 12) {
        return shoreline_color(shoreline_mask);
    }
    if (debug_view == 13) {
        return land_mask_color(land_mask);
    }
    if (debug_view == 14) {
        return moisture_color(moisture);
    }
    if (debug_view == 15) {
        return temperature_color(temperature);
    }
    if (debug_view == 16) {
        return roughness_color(roughness);
    }
    if (debug_view == 17) {
        return lod_color();
    }
    return final_color(normal, material, normalized_elevation, normalized_slope, 0.5, 0.5);
}

void main() {
    vec4 bounds = patch_bounds();
    vec2 sample_uv = edge_transition_uv(in_uv);
    float u = mix(bounds.x, bounds.z, sample_uv.x);
    float v = mix(bounds.y, bounds.w, sample_uv.y);
    vec3 sphere_normal = normalize(planet_surface_cube_face_point(in_patch_id.x, u, v));
    float height_m = planet_surface_terrain_height_m(sphere_normal);
    vec3 normal =
        planet_surface_terrain_normal(in_patch_id.x, u, v, in_patch_id.y, sphere_normal);
    float normalized_elevation = planet_surface_normalized_elevation(height_m);
    float normalized_slope = planet_surface_normalized_slope(sphere_normal, normal);
    float height_above_sea_m = planet_surface_height_above_sea_m(height_m);
    float water_depth_m = planet_surface_water_depth_m(height_m);
    float normalized_bathymetry = planet_surface_normalized_bathymetry(height_m);
    float shoreline_mask = planet_surface_shoreline_mask(height_m);
    float land_mask = planet_surface_land_mask(height_above_sea_m);
    float moisture = planet_surface_moisture(sphere_normal, shoreline_mask, normalized_elevation);
    float temperature = planet_surface_temperature(sphere_normal, normalized_elevation);
    uint material =
        planet_surface_material_id(height_above_sea_m, water_depth_m, shoreline_mask,
                                   normalized_elevation, normalized_slope, moisture, temperature);
    float roughness = planet_surface_material_roughness(material, normalized_slope, moisture);
    vec3 world_position = sphere_normal * (surface_frame.render_origin_radius.w + height_m);
    world_position -= normal * surface_frame.terrain_options.w * in_skirt;
    vec3 render_position = world_position - surface_frame.render_origin_radius.xyz;

    out_color = vertex_color(sphere_normal, normal, height_m, material, normalized_elevation,
                             normalized_slope, shoreline_mask, land_mask, moisture, temperature,
                             roughness);
    out_normal = normal;
    out_uv = in_uv;
    out_render_position = render_position;
    out_sphere_normal = sphere_normal;
    out_surface_field = vec4(height_above_sea_m, normalized_elevation, normalized_slope,
                             shoreline_mask);
    out_climate_field = vec4(normalized_bathymetry, moisture, temperature, roughness);
    gl_Position = surface_frame.view_projection * vec4(render_position, 1.0);
}
