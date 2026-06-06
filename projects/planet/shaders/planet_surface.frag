#version 450
#extension GL_GOOGLE_include_directive : require

#include "planet_atmosphere.glsl"

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_render_position;
layout(location = 4) in vec3 in_sphere_normal;
layout(location = 5) in vec4 in_surface_field;
layout(location = 6) in vec4 in_climate_field;

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

layout(location = 0) out vec4 out_color;

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

float grid_wire_alpha(vec2 uv) {
    vec2 grid_uv = uv * patch_resolution_option();
    vec2 cell_fraction = fract(grid_uv);
    vec2 cell_edge_distance = min(cell_fraction, 1.0 - cell_fraction);
    float distance_to_line = min(cell_edge_distance.x, cell_edge_distance.y);
    float width = max(max(fwidth(grid_uv.x), fwidth(grid_uv.y)) * 0.42, 0.0015);
    float axis_wire = 1.0 - smoothstep(width, width * 2.0, distance_to_line);

    // Patch grid indices split each cell along the edge from local (1, 0) to (0, 1).
    float distance_to_diagonal = abs(cell_fraction.x + cell_fraction.y - 1.0) * 0.70710678;
    float diagonal_wire = 1.0 - smoothstep(width, width * 2.0, distance_to_diagonal);
    return max(axis_wire, diagonal_wire);
}

float celestial_plane_band(vec3 sphere_normal, vec3 plane_normal, float width) {
    float distance_to_plane = abs(dot(sphere_normal, normalize(plane_normal)));
    float edge_width = max(fwidth(distance_to_plane) * 2.0, 0.0015);
    return 1.0 - smoothstep(width, width + edge_width, distance_to_plane);
}

float celestial_direction_marker(vec3 sphere_normal, vec3 direction, float radius) {
    float alignment = dot(sphere_normal, normalize(direction));
    float edge_width = max(fwidth(alignment) * 2.0, 0.0015);
    return smoothstep(cos(radius) - edge_width, cos(radius), alignment);
}

vec3 surface_haze_color(vec3 haze_color, vec3 sphere_normal, vec3 light_dir, vec3 view_dir,
                        float distance_haze) {
    vec3 up = normalize(sphere_normal);
    PlanetAtmosphereTerms terms =
        planet_atmosphere_terms(normalize(view_dir), up, normalize(light_dir));
    float distance_horizon = smoothstep(0.32, 0.92, distance_haze);
    float horizon = max(terms.horizon, distance_horizon);
    vec3 scatter =
        planet_atmosphere_scatter_color(terms.sun_elevation, terms.toward_sun, horizon);
    float daylight_visibility = smoothstep(-0.10, 0.22, terms.sun_elevation);
    float twilight_visibility = terms.twilight * (0.55 + 0.45 * terms.toward_sun);
    float scatter_weight =
        clamp(distance_horizon * max(daylight_visibility * 0.22, twilight_visibility * 0.70),
              0.0, 0.72);
    vec3 night_haze = mix(vec3(0.006, 0.009, 0.020), haze_color * 0.42,
                          clamp(daylight_visibility, 0.0, 1.0));
    vec3 lit_haze = mix(haze_color, scatter, scatter_weight);
    return mix(night_haze, lit_haze, max(daylight_visibility, terms.twilight * 0.78));
}

vec3 celestial_planes_color() {
    vec3 sphere_normal = normalize(in_sphere_normal);
    float equator = celestial_plane_band(sphere_normal, surface_frame.celestial_equator_plane.xyz, 0.010);
    float ecliptic = celestial_plane_band(sphere_normal, surface_frame.celestial_ecliptic_plane.xyz, 0.010);
    float moon_orbit = celestial_plane_band(sphere_normal, surface_frame.celestial_moon_orbit_plane.xyz, 0.010);
    float sun_marker = celestial_direction_marker(sphere_normal, surface_frame.celestial_sun_direction.xyz, 0.050);
    float moon_marker = celestial_direction_marker(sphere_normal, surface_frame.celestial_moon_direction.xyz, 0.045);

    vec3 base = vec3(0.018, 0.030, 0.052);
    base = mix(base, vec3(0.18, 0.42, 0.72), clamp(sphere_normal.y * 0.5 + 0.5, 0.0, 1.0) * 0.32);
    vec3 color = base;
    color = mix(color, vec3(0.30, 0.78, 1.00), equator * 0.88);
    color = mix(color, vec3(1.00, 0.70, 0.24), ecliptic * 0.92);
    color = mix(color, vec3(0.82, 0.52, 1.00), moon_orbit * 0.90);
    color = mix(color, vec3(1.00, 0.96, 0.42), sun_marker);
    color = mix(color, vec3(0.70, 0.82, 1.00), moon_marker);
    return color;
}

uint fragment_material_id() {
    float height_above_sea_m = in_surface_field.x;
    float normalized_elevation = in_surface_field.y;
    float normalized_slope = in_surface_field.z;
    float shoreline_mask = in_surface_field.w;
    float water_depth_m = in_climate_field.x * max(surface_frame.field_options.y, 0.0001);
    float moisture = in_climate_field.y;
    float temperature = in_climate_field.z;
    return planet_surface_material_id(height_above_sea_m, water_depth_m, shoreline_mask,
                                      normalized_elevation, normalized_slope, moisture,
                                      temperature);
}

vec3 fragment_material_albedo(uint material) {
    float normalized_elevation = in_surface_field.y;
    float normalized_slope = in_surface_field.z;
    float shoreline_mask = clamp(in_surface_field.w, 0.0, 1.0);
    float normalized_bathymetry = clamp(in_climate_field.x, 0.0, 1.0);
    float moisture = clamp(in_climate_field.y, 0.0, 1.0);
    float temperature = clamp(in_climate_field.z, 0.0, 1.0);
    vec3 base = planet_surface_material_color(material, normalized_elevation, normalized_slope,
                                              moisture, temperature);
    if (material == 0U || material == 1U) {
        vec3 shallow = vec3(0.055, 0.245, 0.300);
        return mix(base, shallow, (1.0 - normalized_bathymetry) * 0.36);
    }
    if (material == 2U) {
        return mix(base, vec3(0.72, 0.62, 0.38), shoreline_mask * 0.40);
    }
    if (material == 3U) {
        vec3 warm_dry = vec3(0.34, 0.30, 0.16);
        return mix(base, warm_dry, (1.0 - moisture) * temperature * 0.20);
    }
    if (material == 4U) {
        return mix(base, vec3(0.56, 0.52, 0.46), normalized_slope * 0.22);
    }
    return base;
}

void main() {
    if (debug_view_option() == 13) {
        float patch_edge = min(min(in_uv.x, in_uv.y), min(1.0 - in_uv.x, 1.0 - in_uv.y));
        float patch_wire = 1.0 - smoothstep(0.0, 0.01, patch_edge);
        float mesh_wire = max(grid_wire_alpha(in_uv), patch_wire);
        vec3 base = mix(vec3(0.025, 0.030, 0.040), in_color * 0.20, 0.35);
        vec3 line = mix(vec3(0.42, 0.56, 0.68), vec3(0.88, 0.66, 0.24), patch_wire);
        float wire_opacity = mix(0.45, 0.70, patch_wire);
        out_color = vec4(mix(base, line, mesh_wire * wire_opacity), 1.0);
        return;
    }
    if (debug_view_option() == 14) {
        out_color = vec4(celestial_planes_color(), 1.0);
        return;
    }

    vec3 normal = normalize(in_normal);
    vec3 light_dir = normalize(surface_frame.light_direction_debug.xyz);
    float final_view = floor(surface_frame.surface_options.x) < 0.5 ? 1.0 : 0.0;
    uint material = fragment_material_id();
    vec3 albedo = final_view > 0.5 ? fragment_material_albedo(material) : in_color;
    float ndotl = max(dot(normal, light_dir), 0.0);
    float wrap = max(dot(normal, light_dir) * 0.5 + 0.5, 0.0);
    vec3 haze_color = surface_frame.haze_color_direct.rgb;
    float direct_intensity = max(surface_frame.haze_color_direct.w, 0.0);
    float ambient_intensity = max(surface_frame.atmosphere_options.x, 0.0);
    vec3 sky = haze_color * pow(wrap, 1.8) * 0.18;
    vec3 color = albedo * (haze_color * ambient_intensity + direct_intensity * ndotl) + sky;
    vec3 to_camera = normalize(surface_frame.camera_horizon.xyz - in_render_position);
    vec3 half_vector = normalize(light_dir + to_camera);
    float roughness = clamp(in_climate_field.w, 0.05, 0.98);
    float specular_power = mix(96.0, 14.0, roughness);
    float specular = pow(max(dot(normal, half_vector), 0.0), specular_power) *
                     (1.0 - roughness);
    float water_specular = material <= 1U ? 1.0 : 0.0;
    float land_specular = material == 2U ? 0.16 : 0.05;
    color += haze_color * direct_intensity * specular *
             mix(land_specular, 0.42 + pow(1.0 - max(dot(normal, to_camera), 0.0), 5.0) * 0.28,
                 water_specular) *
             final_view;
    float edge = min(min(in_uv.x, in_uv.y), min(1.0 - in_uv.x, 1.0 - in_uv.y));
    float wire = 1.0 - smoothstep(0.0, 0.015, edge);
    float wire_enabled = fract(surface_frame.surface_options.x) > 0.1 ? 1.0 : 0.0;
    color = mix(color, vec3(0.92, 0.96, 1.0), wire * wire_enabled);
    float view_distance = length(in_render_position - surface_frame.camera_horizon.xyz);
    float horizon_distance = max(surface_frame.camera_horizon.w, 1.0);
    float haze_strength = clamp(surface_frame.atmosphere_options.y, 0.0, 1.0);
    float haze_start = clamp(surface_frame.atmosphere_options.z, 0.0, 1.0);
    float haze_end = clamp(max(surface_frame.atmosphere_options.w, haze_start + 0.001), 0.0, 1.5);
    float distance_ratio = view_distance / horizon_distance;
    float band_haze = smoothstep(haze_start, haze_end, distance_ratio);
    float optical_haze =
        1.0 - exp(-max(distance_ratio - haze_start * 0.35, 0.0) / max(haze_end * 0.42, 0.05));
    float horizon_graze = pow(clamp(distance_ratio, 0.0, 1.35), 1.45);
    float haze = clamp(max(band_haze * 0.58, optical_haze * 0.72) + horizon_graze * 0.10, 0.0, 1.0) *
                 haze_strength;
    vec3 view_dir = normalize(in_render_position - surface_frame.camera_horizon.xyz);
    vec3 final_haze_color =
        surface_haze_color(haze_color, normalize(in_sphere_normal), light_dir, view_dir, haze);
    color = mix(color, final_haze_color, haze * final_view);
    out_color = vec4(color, 1.0);
}
