#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 in_local_xz_m;
layout(location = 1) in vec2 in_patch_uv;
layout(location = 2) in float in_level;
layout(location = 3) in float in_blend;

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
    vec4 moon_color_intensity;
    vec4 local_origin_options;
    vec4 local_right_outer;
    vec4 local_up_height;
    vec4 local_forward_scale;
    vec4 local_detail_options;
} surface_frame;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec3 out_render_position;
layout(location = 4) out vec3 out_sphere_normal;
layout(location = 5) out vec4 out_surface_field;
layout(location = 6) out vec4 out_climate_field;
layout(location = 7) out vec4 out_local_detail;
layout(location = 8) out float out_local_detail_delta;

uint packed_patch_lod_option() {
    return uint(surface_frame.surface_options.y + 0.5);
}

float patch_resolution_option() {
    return float(max(packed_patch_lod_option() / 256U, 1U));
}

float patches_per_face_option() {
    return float(max((packed_patch_lod_option() / 16U) & 15U, 1U));
}

int debug_view_option() {
    return int(floor(surface_frame.surface_options.x));
}

#include "planet_surface_field.glsl"

vec3 local_detail_world_plane_position(vec2 local_xz) {
    return surface_frame.local_origin_options.xyz +
           normalize(surface_frame.local_right_outer.xyz) * local_xz.x +
           normalize(surface_frame.local_forward_scale.xyz) * local_xz.y;
}

float local_detail_land_blend(float height_m) {
    float height_above_sea = planet_surface_height_above_sea_m(height_m);
    float shoreline_width = max(surface_frame.field_options.z, 1.0);
    return smoothstep(shoreline_width * 0.12, shoreline_width * 0.85, height_above_sea);
}

struct PlanetLocalDetailFeatureContributions {
    float ridge;
    float channel;
    float plain;
    float net;
};

vec3 local_detail_planet_anchor(vec3 sphere_normal, float base_height_m) {
    return sphere_normal * (surface_frame.render_origin_radius.w + base_height_m);
}

PlanetLocalDetailFeatureContributions local_detail_feature_contributions(
    vec2 local_xz, vec3 sphere_normal, float base_height_m) {
    float scale = max(surface_frame.local_forward_scale.w, 1.0);
    uint seed = uint(surface_frame.terrain_options.z + 0.5) + 3109U;
    vec3 p = local_detail_planet_anchor(sphere_normal, base_height_m) / scale;
    p += vec3(local_xz.x, base_height_m * 0.21, local_xz.y) / (scale * 9.0);

    PlanetTerrainFeatureContext features = planet_surface_terrain_feature_context(sphere_normal);
    float ridge_gate = features.mountain_belt * features.relief_gate * features.land_mask;
    float channel_gate =
        features.valley_network * features.relief_gate * features.land_mask *
        (0.45 + features.mountain_belt * 0.55);
    float plain_gate = features.plain_gate * features.land_mask *
                       (1.0 - smoothstep(0.24, 0.74, features.mountain_belt));

    float ridge_source =
        planet_surface_fbm(p * 0.92 + vec3(-4.2, 2.6, 7.1), seed + 317U, 4U);
    float ridge_line = pow(max(1.0 - abs(ridge_source * 1.18), 0.0), 2.8);
    float ridge_breakup =
        planet_surface_fbm(p * 1.72 + vec3(0.8, 5.3, -3.9), seed + 733U, 3U);
    float ridge = (ridge_line + ridge_breakup * 0.10 - 0.16) * ridge_gate * 0.95;

    float channel_source =
        planet_surface_fbm(p * 0.54 + vec3(6.4, -3.3, 1.8), seed + 401U, 4U);
    float channel_line = pow(max(1.0 - abs(channel_source * 1.34), 0.0), 3.2);
    float channel_breakup =
        planet_surface_fbm(p * 1.26 + vec3(-7.2, 1.4, 4.9), seed + 811U, 3U);
    float channel = channel_line * (0.88 + channel_breakup * 0.12) * channel_gate * 0.58;

    float plain_noise =
        planet_surface_fbm(p * 0.22 + vec3(1.1, 8.5, -5.7), seed + 977U, 3U);
    float plain = plain_noise * plain_gate * 0.18;

    float net = ridge - channel + plain;
    return PlanetLocalDetailFeatureContributions(ridge, channel, plain, net);
}

float local_detail_height_delta_m(vec2 local_xz, vec3 sphere_normal,
                                  float base_height_m, float ownership) {
    float active_weight = surface_frame.local_origin_options.w;
    float height_strength = max(surface_frame.local_up_height.w, 0.0);
    if (active_weight <= 0.0 || height_strength <= 0.0 || ownership <= 0.0) {
        return 0.0;
    }

    PlanetLocalDetailFeatureContributions features =
        local_detail_feature_contributions(local_xz, sphere_normal, base_height_m);
    float land = local_detail_land_blend(base_height_m);
    float slope_gate = smoothstep(-0.08, 0.18, dot(normalize(sphere_normal),
                                                   normalize(surface_frame.local_up_height.xyz)));
    return features.net * height_strength * land * slope_gate * ownership * active_weight;
}

vec3 local_detail_world_position(vec2 local_xz, out vec3 sphere_normal, out float height_m,
                                 out float detail_height_m) {
    vec3 plane_position = local_detail_world_plane_position(local_xz);
    sphere_normal = normalize(plane_position);
    float base_height_m = planet_surface_terrain_height_m(sphere_normal);
    float ownership = clamp(in_blend, 0.0, 1.0);
    detail_height_m = local_detail_height_delta_m(local_xz, sphere_normal, base_height_m,
                                                  ownership);
    height_m = base_height_m + detail_height_m;
    return sphere_normal * (surface_frame.render_origin_radius.w + height_m);
}

vec3 local_detail_normal(vec2 local_xz, vec3 sphere_normal) {
    float step_m = max(surface_frame.local_detail_options.z * 1.25, 1.0);
    vec3 unused_normal;
    float unused_height;
    float unused_detail;
    vec3 x0 = local_detail_world_position(local_xz - vec2(step_m, 0.0), unused_normal,
                                          unused_height, unused_detail);
    vec3 x1 = local_detail_world_position(local_xz + vec2(step_m, 0.0), unused_normal,
                                          unused_height, unused_detail);
    vec3 z0 = local_detail_world_position(local_xz - vec2(0.0, step_m), unused_normal,
                                          unused_height, unused_detail);
    vec3 z1 = local_detail_world_position(local_xz + vec2(0.0, step_m), unused_normal,
                                          unused_height, unused_detail);
    vec3 normal = cross(x1 - x0, z1 - z0);
    if (length(normal) <= 0.0000001) {
        return sphere_normal;
    }
    normal = normalize(normal);
    if (dot(normal, sphere_normal) < 0.0) {
        normal = -normal;
    }
    return normal;
}

vec3 local_detail_color(vec3 sphere_normal, vec3 normal, float height_m, uint material,
                        float normalized_elevation, float normalized_slope,
                        float shoreline_mask, float land_mask, float moisture,
                        float temperature, float roughness) {
    int debug_view = debug_view_option();
    if (debug_view == 8) {
        float height_scale = max(surface_frame.terrain_options.x, 1.0);
        float t = clamp(height_m / height_scale, -1.0, 1.0) * 0.5 + 0.5;
        return mix(vec3(0.04, 0.12, 0.36), vec3(0.92, 0.88, 0.74), t);
    }
    if (debug_view == 9) {
        return planet_surface_slope_color(normalized_slope);
    }
    if (debug_view == 10) {
        return planet_surface_material_debug_color(material);
    }
    if (debug_view == 11) {
        float t = clamp(planet_surface_normalized_bathymetry(height_m), 0.0, 1.0);
        return mix(vec3(0.04, 0.28, 0.44), vec3(0.01, 0.06, 0.88), t);
    }
    if (debug_view == 12) {
        return mix(vec3(0.03, 0.12, 0.20), vec3(0.95, 0.82, 0.32),
                   clamp(shoreline_mask, 0.0, 1.0));
    }
    if (debug_view == 13) {
        return mix(vec3(0.02, 0.10, 0.30), vec3(0.20, 0.62, 0.14),
                   clamp(land_mask, 0.0, 1.0));
    }
    if (debug_view == 14) {
        return mix(vec3(0.56, 0.42, 0.18), vec3(0.04, 0.46, 0.72),
                   clamp(moisture, 0.0, 1.0));
    }
    if (debug_view == 15) {
        return mix(vec3(0.08, 0.24, 0.82), vec3(0.95, 0.44, 0.10),
                   clamp(temperature, 0.0, 1.0));
    }
    if (debug_view == 16) {
        return mix(vec3(0.08, 0.09, 0.12), vec3(0.90, 0.90, 0.96),
                   clamp(roughness, 0.0, 1.0));
    }
    return planet_surface_material_color(material, normalized_elevation, normalized_slope,
                                        moisture, temperature);
}

void main() {
    vec3 sphere_normal;
    float height_m;
    float detail_height_m;
    vec3 world_position = local_detail_world_position(in_local_xz_m, sphere_normal, height_m,
                                                      detail_height_m);
    vec3 normal = local_detail_normal(in_local_xz_m, sphere_normal);
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
    vec3 render_position = world_position - surface_frame.render_origin_radius.xyz;

    out_color = local_detail_color(sphere_normal, normal, height_m, material,
                                   normalized_elevation, normalized_slope, shoreline_mask,
                                   land_mask, moisture, temperature, roughness);
    out_normal = normal;
    out_uv = in_patch_uv;
    out_render_position = render_position;
    out_sphere_normal = sphere_normal;
    out_surface_field = vec4(height_above_sea_m, normalized_elevation, normalized_slope,
                             shoreline_mask);
    out_climate_field = vec4(normalized_bathymetry, moisture, temperature, roughness);
    out_local_detail = vec4(in_local_xz_m, in_level, in_blend);
    out_local_detail_delta = detail_height_m / max(surface_frame.local_up_height.w, 1.0);
    gl_Position = surface_frame.view_projection * vec4(render_position, 1.0);
}
