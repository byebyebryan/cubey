#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/procedural/noise.glsl"
#include "terrain_source.glsl"

layout(set = 0, binding = 0, std140) uniform TerrainSourceUniforms {
    TerrainSourceGpuParameters source;
} terrain_uniforms;

layout(push_constant) uniform TerrainPushConstants {
    mat4 view_projection;
    vec4 camera_position_vertical_scale;
    vec4 light_direction_intensity;
    vec4 light_color_debug_view;
    vec4 ambient_color_outer_extent;
} pc;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in float frag_base_height_m;
layout(location = 2) in float frag_height_m;
layout(location = 3) in float frag_weathering_delta_m;
layout(location = 4) flat in float frag_lod;
layout(location = 5) in vec3 frag_source_normal;
layout(location = 6) flat in float frag_cell_size_m;
layout(location = 7) flat in float frag_child_half_extent_m;

layout(location = 0) out vec4 out_color;

vec3 terrain_geometric_normal() {
    vec3 normal = normalize(cross(dFdy(frag_world_position), dFdx(frag_world_position)));
    return normal.y < 0.0 ? -normal : normal;
}

bool terrain_covered_by_finer_lod(vec2 world_xz) {
    if (frag_child_half_extent_m <= 0.0) {
        return false;
    }
    float child_cell_size_m = frag_cell_size_m * 0.5;
    vec2 child_origin = floor(pc.camera_position_vertical_scale.xz / child_cell_size_m) *
        child_cell_size_m;
    vec2 child_position = abs(world_xz - child_origin);
    float owned_half_extent_m = max(frag_child_half_extent_m - child_cell_size_m, 0.0);
    return max(child_position.x, child_position.y) < owned_half_extent_m;
}

float terrain_color_noise(vec2 world_xz) {
    float broad = cubey_proc_value_noise_pcg_2d(world_xz * 0.0022);
    float fine = cubey_proc_value_noise_pcg_2d(world_xz * 0.013 + vec2(19.0, -31.0));
    return mix(broad, fine, 0.32);
}

float terrain_material_relief(vec2 world_xz, float pixel_footprint_m) {
    float broad_visibility = 1.0 - smoothstep(7.0, 26.0, pixel_footprint_m);
    float fine_visibility = 1.0 - smoothstep(1.5, 7.0, pixel_footprint_m);
    float broad = cubey_proc_value_noise_pcg_2d(world_xz * 0.045 + vec2(-7.0, 13.0)) - 0.5;
    float fine = cubey_proc_value_noise_pcg_2d(world_xz * 0.18 + vec2(41.0, 5.0)) - 0.5;
    return broad * broad_visibility * 2.0 + fine * fine_visibility * 0.4;
}

vec3 terrain_material_normal(vec3 source_normal, vec2 world_xz) {
    float pixel_footprint_m = max(length(dFdx(world_xz)), length(dFdy(world_xz)));
    const float step_m = 0.75;
    float center = terrain_material_relief(world_xz, pixel_footprint_m);
    vec2 gradient = vec2(
        terrain_material_relief(world_xz + vec2(step_m, 0.0), pixel_footprint_m) - center,
        terrain_material_relief(world_xz + vec2(0.0, step_m), pixel_footprint_m) - center) /
        step_m;
    return normalize(vec3(source_normal.x - gradient.x * 0.9, source_normal.y,
        source_normal.z - gradient.y * 0.9));
}

vec3 terrain_lod_color(float value) {
    const vec3 colors[8] = vec3[8](
        vec3(0.12, 0.72, 0.34), vec3(0.22, 0.66, 0.82),
        vec3(0.32, 0.42, 0.88), vec3(0.60, 0.32, 0.84),
        vec3(0.88, 0.34, 0.62), vec3(0.94, 0.48, 0.24),
        vec3(0.88, 0.72, 0.20), vec3(0.78, 0.84, 0.35));
    int index = clamp(int(round(value * 7.0)), 0, 7);
    return colors[index];
}

void main() {
    vec3 source_normal = normalize(mix(frag_source_normal, terrain_geometric_normal(), 0.12));
    if (terrain_covered_by_finer_lod(frag_world_position.xz)) {
        discard;
    }
    float normalized_height = clamp(
        (frag_height_m - terrain_uniforms.source.elevation.x) /
            max(terrain_uniforms.source.elevation.y, 1.0),
        0.0, 1.0);
    float slope = 1.0 - clamp(source_normal.y, 0.0, 1.0);
    int debug_view = int(round(pc.light_color_debug_view.w));

    if (debug_view == 1) {
        vec3 low = vec3(0.08, 0.18, 0.26);
        vec3 mid = vec3(0.34, 0.48, 0.30);
        vec3 high = vec3(0.92, 0.91, 0.86);
        vec3 color = mix(low, mid, smoothstep(0.04, 0.52, normalized_height));
        color = mix(color, high, smoothstep(0.50, 0.96, normalized_height));
        out_color = vec4(color, 1.0);
        return;
    }
    if (debug_view == 2) {
        float base_normalized = clamp(
            (frag_base_height_m - terrain_uniforms.source.elevation.x) /
                max(terrain_uniforms.source.elevation.y, 1.0),
            0.0, 1.0);
        out_color = vec4(vec3(base_normalized), 1.0);
        return;
    }
    if (debug_view == 3) {
        out_color = vec4(mix(vec3(0.10, 0.25, 0.58), vec3(0.92, 0.36, 0.12),
            smoothstep(0.02, 0.78, slope)), 1.0);
        return;
    }
    if (debug_view == 4) {
        float extent = max(terrain_uniforms.source.weathering.y * 0.03, 0.001);
        float signed_delta = clamp(frag_weathering_delta_m / extent, -1.0, 1.0);
        vec3 neutral = vec3(0.13, 0.14, 0.15);
        vec3 color = signed_delta < 0.0
            ? mix(neutral, vec3(0.16, 0.48, 0.86), -signed_delta)
            : mix(neutral, vec3(0.92, 0.36, 0.12), signed_delta);
        out_color = vec4(color, 1.0);
        return;
    }
    if (debug_view == 5) {
        out_color = vec4(terrain_lod_color(frag_lod), 1.0);
        return;
    }

    float variation = terrain_color_noise(frag_world_position.xz);
    vec3 grass = vec3(0.19, 0.30, 0.13) * mix(0.78, 1.18, variation);
    vec3 soil = vec3(0.34, 0.27, 0.18) * mix(0.84, 1.12, variation);
    vec3 rock = vec3(0.43, 0.42, 0.39) * mix(0.82, 1.16, variation);
    vec3 snow = vec3(0.86, 0.88, 0.86) * mix(0.94, 1.04, variation);
    vec3 base_color = mix(grass, soil, smoothstep(0.18, 0.48, slope));
    base_color = mix(base_color, rock, smoothstep(0.34, 0.72, slope));
    float snow_mask = smoothstep(0.55, 0.84, normalized_height) *
        (1.0 - smoothstep(0.38, 0.78, slope));
    base_color = mix(base_color, snow, snow_mask);

    vec3 normal = terrain_material_normal(source_normal, frag_world_position.xz);
    vec3 light_direction = normalize(pc.light_direction_intensity.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    vec3 sun = pc.light_color_debug_view.xyz *
        (pc.light_direction_intensity.w * diffuse);
    vec3 ambient = pc.ambient_color_outer_extent.xyz *
        (0.72 + 0.28 * clamp(normal.y, 0.0, 1.0));
    vec3 color = base_color * (ambient + sun);

    float camera_distance = distance(pc.camera_position_vertical_scale.xyz, frag_world_position);
    float fog = smoothstep(pc.ambient_color_outer_extent.w * 0.52,
        pc.ambient_color_outer_extent.w * 1.15, camera_distance);
    color = mix(color, vec3(0.46, 0.58, 0.68), fog * 0.52);
    out_color = vec4(color, 1.0);
}
