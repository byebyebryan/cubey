#version 450

layout(set = 0, binding = 0) uniform sampler2D displacement_cascade0_texture;
layout(set = 0, binding = 1) uniform sampler2D displacement_cascade1_texture;
layout(set = 0, binding = 2) uniform sampler2D displacement_cascade2_texture;
layout(set = 0, binding = 3) uniform sampler2D displacement_cascade3_texture;
layout(set = 0, binding = 4) uniform sampler2D displacement_cascade4_texture;
layout(set = 0, binding = 19) uniform OceanFeatureParams {
    vec4 feature_options;
    vec4 feature_options2;
    vec4 material_options;
    vec4 fade_options;
    vec4 cascade_options;
    vec4 self_shadow_options;
    vec4 surface_frame_options;
    vec4 surface_curve_options;
    vec4 far_field_options;
    vec4 far_field_options2;
    vec4 far_detail_options;
    vec4 cloud_shadow_world_to_uv_x;
    vec4 cloud_shadow_world_to_uv_y;
    vec4 cloud_lighting_options;
    vec4 sun_light_direction_intensity;
    vec4 sun_light_color;
    vec4 moon_light_direction_intensity;
    vec4 moon_light_color;
} ocean_features;

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 patch_bounds;
    vec4 debug_options;
    vec4 inspection_options;
    vec4 tile_lengths;
    vec4 displacement_scales;
    vec4 normal_scales;
    vec4 cascade4_options;
    vec4 water_color;
    vec4 foam_color;
} ocean;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_displacement;
layout(location = 2) out vec2 frag_sample_position;
layout(location = 3) out vec4 frag_wave;
layout(location = 4) out float frag_patch_alpha;
layout(location = 5) noperspective out vec3 frag_barycentric;
layout(location = 6) out float frag_mesh_cell_size;
layout(location = 7) out vec3 frag_surface_up;
layout(location = 8) out float frag_surface_curve_drop;

const float OCEAN_MESH_TRANSITION_CELLS = 16.0;
const float OCEAN_MESH_MAX_TRANSITION_RATIO = 0.35;
const float OCEAN_SHAPE_ANTI_REPEAT_WEIGHT = 0.32;
const float OCEAN_CASCADE_DISTANCE_FADE_START_WAVES = 8.0;
const float OCEAN_CASCADE_DISTANCE_FADE_END_WAVES = 24.0;
const float OCEAN_CASCADE_MESH_FULL_TILE_CELL_DIVISOR = 10.0;
const float OCEAN_CASCADE_MESH_ZERO_TILE_CELL_DIVISOR = 4.0;

vec2 triangle_corner(uint vertex_in_cell) {
    if (vertex_in_cell == 0u) {
        return vec2(0.0, 0.0);
    }
    if (vertex_in_cell == 1u) {
        return vec2(1.0, 0.0);
    }
    if (vertex_in_cell == 2u) {
        return vec2(0.0, 1.0);
    }
    if (vertex_in_cell == 3u) {
        return vec2(0.0, 1.0);
    }
    if (vertex_in_cell == 4u) {
        return vec2(1.0, 0.0);
    }
    return vec2(1.0, 1.0);
}

vec3 triangle_barycentric(uint vertex_in_cell) {
    if (vertex_in_cell == 0u || vertex_in_cell == 3u) {
        return vec3(1.0, 0.0, 0.0);
    }
    if (vertex_in_cell == 1u || vertex_in_cell == 4u) {
        return vec3(0.0, 1.0, 0.0);
    }
    return vec3(0.0, 0.0, 1.0);
}

vec2 rotate2(vec2 value, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * value.x - s * value.y, s * value.x + c * value.y);
}

float shape_anti_repeat_angle(uint cascade) {
    return 0.47 + float(cascade) * 1.173;
}

vec2 shape_anti_repeat_offset(uint cascade) {
    float slot = float(cascade);
    return vec2(347.0 + slot * 193.0, -911.0 + slot * 467.0);
}

vec3 rotate_displacement_xz(vec3 displacement, float angle) {
    vec2 xz = rotate2(displacement.xz, angle);
    return vec3(xz.x, displacement.y, xz.y);
}

vec2 clipmap_patch_position(vec2 uv) {
    return vec2(mix(ocean.patch_bounds.x, ocean.patch_bounds.y, uv.x),
                mix(ocean.patch_bounds.z, ocean.patch_bounds.w, uv.y));
}

float clipmap_transition_width(float coarse_cell_size, float boundary_extent) {
    const float preferred = coarse_cell_size * OCEAN_MESH_TRANSITION_CELLS;
    const float maximum = boundary_extent * OCEAN_MESH_MAX_TRANSITION_RATIO;
    return max(0.001, min(preferred, maximum));
}

float clipmap_patch_alpha(vec2 patch_position, float patch_cell_size) {
    if (ocean.debug_options.y >= ocean.debug_options.z - 0.5) {
        return 1.0;
    }
    float outer_extent = max(max(abs(ocean.patch_bounds.x), abs(ocean.patch_bounds.y)),
                             max(abs(ocean.patch_bounds.z), abs(ocean.patch_bounds.w)));
    float transition_width = clipmap_transition_width(patch_cell_size * 2.0, outer_extent);
    float radius = max(abs(patch_position.x), abs(patch_position.y));
    return 1.0 - smoothstep(outer_extent - transition_width, outer_extent, radius);
}

float cascade_tile_length(uint cascade) {
    if (cascade == 0u) {
        return ocean.tile_lengths.x;
    }
    if (cascade == 1u) {
        return ocean.tile_lengths.y;
    }
    if (cascade == 2u) {
        return ocean.tile_lengths.z;
    }
    if (cascade == 3u) {
        return ocean.tile_lengths.w;
    }
    return ocean.cascade4_options.x;
}

float cascade_displacement_scale(uint cascade) {
    if (cascade == 0u) {
        return ocean.displacement_scales.x;
    }
    if (cascade == 1u) {
        return ocean.displacement_scales.y;
    }
    if (cascade == 2u) {
        return ocean.displacement_scales.z;
    }
    if (cascade == 3u) {
        return ocean.displacement_scales.w;
    }
    return ocean.cascade4_options.y;
}

vec4 sample_displacement(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(displacement_cascade0_texture, uv);
    }
    if (cascade == 1u) {
        return texture(displacement_cascade1_texture, uv);
    }
    if (cascade == 2u) {
        return texture(displacement_cascade2_texture, uv);
    }
    if (cascade == 3u) {
        return texture(displacement_cascade3_texture, uv);
    }
    return texture(displacement_cascade4_texture, uv);
}

bool ocean_cascade_enabled(uint cascade) {
    float selected = ocean.inspection_options.x;
    int mask = int(ocean_features.cascade_options.x + 0.5);
    bool feature_enabled = (mask & (1 << int(cascade))) != 0;
    bool selected_enabled = selected < -0.5 || abs(selected - float(cascade)) < 0.5;
    return feature_enabled && selected_enabled;
}

bool ocean_shape_anti_repeat_enabled() {
    return ocean.inspection_options.y > 0.0;
}

float ocean_surface_shape_strength() {
    return max(ocean_features.feature_options.x, 0.0);
}

float ocean_shape_fade_distance_scale() {
    return max(ocean_features.feature_options2.y, 0.001);
}

float cascade_distance_lod_weight(uint cascade, float camera_distance, float start_waves,
                                  float end_waves, float fade_scale) {
    float tile_length = max(cascade_tile_length(cascade), 0.001);
    float start = tile_length * start_waves * fade_scale;
    float end = tile_length * end_waves * fade_scale;
    return 1.0 - smoothstep(start, max(end, start + 0.001), camera_distance);
}

float cascade_mesh_lod_weight(uint cascade, float mesh_cell_size) {
    float tile_length = max(cascade_tile_length(cascade), 0.001);
    float full_cell = tile_length / OCEAN_CASCADE_MESH_FULL_TILE_CELL_DIVISOR;
    float zero_cell = tile_length / OCEAN_CASCADE_MESH_ZERO_TILE_CELL_DIVISOR;
    return 1.0 - smoothstep(full_cell, max(zero_cell, full_cell + 0.001),
                            max(mesh_cell_size, 0.001));
}

float cascade_displacement_lod_weight(uint cascade, float camera_distance, float mesh_cell_size) {
    float distance_weight =
        cascade_distance_lod_weight(cascade, camera_distance,
                                    OCEAN_CASCADE_DISTANCE_FADE_START_WAVES,
                                    OCEAN_CASCADE_DISTANCE_FADE_END_WAVES,
                                    ocean_shape_fade_distance_scale());
    return distance_weight * cascade_mesh_lod_weight(cascade, mesh_cell_size);
}

float horizon_displacement_weight(float camera_distance) {
    return min(exp(-(camera_distance - 150.0) * 0.007 /
                   ocean_shape_fade_distance_scale()),
               1.0);
}

float ocean_water_datum_y() {
    return ocean_features.surface_frame_options.x;
}

float ocean_surface_curvature_strength() {
    return clamp(ocean_features.surface_curve_options.w, 0.0, 1.0);
}

float ocean_surface_planet_radius_m() {
    return max(ocean_features.surface_frame_options.y, 0.001);
}

float ocean_surface_curvature_start_m() {
    return max(ocean_features.surface_curve_options.y, 0.0);
}

float ocean_surface_curvature_end_m() {
    return max(ocean_features.surface_curve_options.z,
               ocean_surface_curvature_start_m() + 0.001);
}

bool ocean_surface_curved_enabled() {
    return ocean_features.surface_curve_options.x > 0.5 &&
           ocean_surface_curvature_strength() > 0.0;
}

vec2 ocean_surface_camera_relative_xz(vec2 local_xz) {
    return local_xz - ocean.camera_time.xz;
}

float ocean_surface_camera_distance(vec2 local_xz) {
    return length(ocean_surface_camera_relative_xz(local_xz));
}

float ocean_surface_curvature_weight(vec2 local_xz) {
    if (!ocean_surface_curved_enabled()) {
        return 0.0;
    }
    return smoothstep(ocean_surface_curvature_start_m(), ocean_surface_curvature_end_m(),
                      ocean_surface_camera_distance(local_xz)) *
           ocean_surface_curvature_strength();
}

float ocean_surface_spherical_drop_y(vec2 local_xz) {
    float radius = ocean_surface_planet_radius_m();
    float distance = min(ocean_surface_camera_distance(local_xz), radius);
    return sqrt(max(radius * radius - distance * distance, 0.0)) - radius;
}

float ocean_surface_drop_y(vec2 local_xz) {
    return ocean_surface_spherical_drop_y(local_xz) * ocean_surface_curvature_weight(local_xz);
}

vec3 ocean_surface_up(vec2 local_xz) {
    float weight = ocean_surface_curvature_weight(local_xz);
    if (weight <= 0.0) {
        return vec3(0.0, 1.0, 0.0);
    }
    float radius = ocean_surface_planet_radius_m();
    vec2 offset = ocean_surface_camera_relative_xz(local_xz);
    float drop = ocean_surface_spherical_drop_y(local_xz);
    vec3 sphere_up = normalize(vec3(offset.x, radius + drop, offset.y));
    return normalize(mix(vec3(0.0, 1.0, 0.0), sphere_up, weight));
}

vec2 ocean_surface_sample_position(vec2 local_xz) {
    return local_xz;
}

vec3 ocean_surface_world_position(vec2 local_xz, vec3 displacement) {
    float drop = ocean_surface_drop_y(local_xz);
    vec3 surface_up = ocean_surface_up(local_xz);
    vec3 base = vec3(local_xz.x + displacement.x, ocean_water_datum_y() + drop,
                     local_xz.y + displacement.z);
    return base + surface_up * displacement.y;
}

vec3 sample_ocean_displacement(uint cascade, vec2 position, float tile_length) {
    vec3 primary = sample_displacement(cascade, position / tile_length).xyz;
    if (!ocean_shape_anti_repeat_enabled()) {
        return primary;
    }

    float angle = shape_anti_repeat_angle(cascade);
    vec2 secondary_position = rotate2(position, angle) + shape_anti_repeat_offset(cascade);
    vec3 secondary = sample_displacement(cascade, secondary_position / tile_length).xyz;
    secondary = rotate_displacement_xz(secondary, -angle);

    float weight = OCEAN_SHAPE_ANTI_REPEAT_WEIGHT * clamp(ocean.inspection_options.y, 0.0, 1.0);
    return (primary + secondary * weight) / (1.0 + weight);
}

void add_displacement(inout vec3 displacement, uint cascade, vec2 position,
                      float camera_distance, float mesh_cell_size) {
    if (!ocean_cascade_enabled(cascade)) {
        return;
    }
    float tile_length = max(cascade_tile_length(cascade), 0.001);
    float lod_weight = cascade_displacement_lod_weight(cascade, camera_distance, mesh_cell_size);
    displacement += sample_ocean_displacement(cascade, position, tile_length) *
                    cascade_displacement_scale(cascade) * lod_weight *
                    ocean_surface_shape_strength();
}

void main() {
    uint cells_x = max(uint(ocean.mesh_options.x + 0.5), 1u);
    uint cells_z = max(uint(ocean.mesh_options.y + 0.5), 1u);
    uint vertex_in_cell = uint(gl_VertexIndex) % 6u;
    uint cell_index = uint(gl_VertexIndex) / 6u;
    uint cell_x = cell_index % cells_x;
    uint cell_z = cell_index / cells_x;

    vec2 uv = (vec2(cell_x, cell_z) + triangle_corner(vertex_in_cell)) /
              vec2(float(cells_x), float(cells_z));
    vec2 camera_xz = ocean.camera_time.xz;
    vec2 patch_size =
        vec2(ocean.patch_bounds.y - ocean.patch_bounds.x,
             ocean.patch_bounds.w - ocean.patch_bounds.z);
    float patch_cell_size =
        max(patch_size.x / max(float(cells_x), 1.0), patch_size.y / max(float(cells_z), 1.0));
    float snap = max(patch_cell_size / exp2(ocean.debug_options.y), 0.001);
    vec2 snapped_center = floor(camera_xz / snap) * snap;
    vec2 patch_position = clipmap_patch_position(uv);
    vec2 base_position = snapped_center + patch_position;
    float camera_distance = length(base_position - camera_xz);

    vec3 displacement = vec3(0.0);
    for (uint cascade = 0u; cascade < 5u; ++cascade) {
        add_displacement(displacement, cascade, base_position, camera_distance, patch_cell_size);
    }
    displacement *= horizon_displacement_weight(camera_distance);

    vec3 world_position = ocean_surface_world_position(base_position, displacement);
    frag_world_position = world_position;
    frag_displacement = displacement;
    frag_sample_position = ocean_surface_sample_position(base_position);
    frag_wave = vec4(displacement.y, 0.0, camera_distance, ocean.foam_color.w);
    frag_patch_alpha = clipmap_patch_alpha(patch_position, patch_cell_size);
    frag_barycentric = triangle_barycentric(vertex_in_cell);
    frag_mesh_cell_size = patch_cell_size;
    frag_surface_up = ocean_surface_up(base_position);
    frag_surface_curve_drop = ocean_surface_drop_y(base_position);
    gl_Position = ocean.view_projection * vec4(world_position, 1.0);
}
