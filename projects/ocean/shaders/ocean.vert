#version 450
#extension GL_GOOGLE_include_directive : require

#include "ocean_macro_waves.glsl"

layout(set = 0, binding = 0) uniform sampler2D displacement_near_texture;
layout(set = 0, binding = 1) uniform sampler2D displacement_mid_texture;
layout(set = 0, binding = 2) uniform sampler2D displacement_far_texture;
layout(set = 0, binding = 3) uniform sampler2D normal_foam_near_texture;
layout(set = 0, binding = 4) uniform sampler2D normal_foam_mid_texture;
layout(set = 0, binding = 5) uniform sampler2D normal_foam_far_texture;
layout(set = 0, binding = 12) uniform sampler2D detail_wave_near_texture;
layout(set = 0, binding = 13) uniform sampler2D detail_wave_mid_texture;
layout(set = 0, binding = 14) uniform sampler2D detail_wave_far_texture;

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
    vec4 patch_bounds;
    vec4 wave_options;
    vec4 detail_options;
    vec4 shading_options;
    vec4 display_transform;
    vec4 disturbance;
    vec4 debug_options;
    vec4 spectrum_options;
    vec4 cascade_options;
    vec4 detail_wave_options;
} ocean;

layout(location = 0) out vec3 frag_world_position;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec4 frag_wave;
layout(location = 3) out vec3 frag_displacement;
layout(location = 4) out vec2 frag_sample_position;
noperspective layout(location = 5) out vec3 frag_barycentric;
layout(location = 6) out float frag_patch_alpha;

const float OCEAN_MESH_TRANSITION_CELLS = 16.0;
const float OCEAN_MESH_MAX_TRANSITION_RATIO = 0.35;

struct SurfaceSample {
    vec3 displacement;
    vec3 normal_sum;
    float foam;
    float weight_sum;
};

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
    uint vertex_in_triangle = vertex_in_cell % 3u;
    if (vertex_in_triangle == 0u) {
        return vec3(1.0, 0.0, 0.0);
    }
    if (vertex_in_triangle == 1u) {
        return vec3(0.0, 1.0, 0.0);
    }
    return vec3(0.0, 0.0, 1.0);
}

float ocean_far_extent() {
    return max(ocean.mesh_options.z, 0.001);
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

float clipmap_square_radius(vec2 position) {
    return max(abs(position.x), abs(position.y));
}

float clipmap_patch_alpha(vec2 patch_position, float patch_cell_size) {
    if (ocean.debug_options.z >= ocean.debug_options.w - 0.5) {
        return 1.0;
    }
    float outer_extent = max(max(abs(ocean.patch_bounds.x), abs(ocean.patch_bounds.y)),
                             max(abs(ocean.patch_bounds.z), abs(ocean.patch_bounds.w)));
    float transition_width = clipmap_transition_width(patch_cell_size * 2.0, outer_extent);
    float radius = clipmap_square_radius(patch_position);
    return 1.0 - smoothstep(outer_extent - transition_width, outer_extent, radius);
}

vec4 sample_displacement(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(displacement_near_texture, uv);
    }
    if (cascade == 1u) {
        return texture(displacement_mid_texture, uv);
    }
    return texture(displacement_far_texture, uv);
}

vec4 sample_normal_foam(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(normal_foam_near_texture, uv);
    }
    if (cascade == 1u) {
        return texture(normal_foam_mid_texture, uv);
    }
    return texture(normal_foam_far_texture, uv);
}

vec4 sample_detail_wave(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(detail_wave_near_texture, uv);
    }
    if (cascade == 1u) {
        return texture(detail_wave_mid_texture, uv);
    }
    return texture(detail_wave_far_texture, uv);
}

float cascade_patch_length(uint cascade) {
    if (cascade == 0u) {
        return ocean.cascade_options.x;
    }
    if (cascade == 1u) {
        return ocean.cascade_options.y;
    }
    return ocean.cascade_options.z;
}

vec2 rotate_position(vec2 position, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(position.x * c - position.y * s, position.x * s + position.y * c);
}

vec2 cascade_sample_position(uint cascade, vec2 position) {
    if (cascade == 1u) {
        return rotate_position(position + vec2(173.2, -91.7), 0.17);
    }
    if (cascade == 2u) {
        return rotate_position(position + vec2(-811.3, 419.6), -0.11);
    }
    return position;
}

float cascade_weight(uint cascade, float camera_distance) {
    if (cascade == 0u) {
        return 1.0 - smoothstep(ocean_far_extent() * 0.18, ocean_far_extent() * 0.55,
                                camera_distance);
    }
    if (cascade == 1u) {
        float fade_in = smoothstep(ocean_far_extent() * 0.10, ocean_far_extent() * 0.22,
                                   camera_distance);
        float fade_out = 1.0 - smoothstep(ocean_far_extent() * 0.58,
                                          ocean_far_extent() * 0.94, camera_distance);
        return fade_in * fade_out;
    }
    return smoothstep(ocean_far_extent() * 0.34, ocean_far_extent() * 0.72, camera_distance);
}

float cascade_displacement_detail_scale(uint cascade) {
    float detail = clamp(ocean.detail_wave_options.w, 0.0, 1.5);
    if (cascade == 0u) {
        return 0.45 * detail;
    }
    if (cascade == 1u) {
        return mix(0.86, 1.26, detail);
    }
    return mix(1.08, 1.58, detail);
}

float cascade_normal_detail_scale(uint cascade) {
    float detail = clamp(ocean.detail_options.y, 0.0, 1.0);
    if (cascade == 0u) {
        return detail * detail * 0.35;
    }
    if (cascade == 1u) {
        return detail * 0.28;
    }
    return detail * 0.18;
}

float cascade_geometry_detail_scale(uint cascade, float camera_distance) {
    if (cascade == 0u) {
        float near = 1.0 - smoothstep(ocean_far_extent() * 0.10,
                                      ocean_far_extent() * 0.36, camera_distance);
        return near;
    }
    if (cascade == 1u) {
        float near = 1.0 - smoothstep(ocean_far_extent() * 0.18,
                                      ocean_far_extent() * 0.46, camera_distance);
        return near * 0.34;
    }
    return 0.0;
}

void add_cascade(inout SurfaceSample sample_value, uint cascade, vec2 position,
                 float camera_distance) {
    float patch_length = cascade_patch_length(cascade);
    float weight = cascade_weight(cascade, camera_distance);
    float displacement_weight = weight * cascade_displacement_detail_scale(cascade);
    vec2 uv = cascade_sample_position(cascade, position) / max(patch_length, 0.001);
    vec4 displacement = sample_displacement(cascade, uv);
    vec4 normal_foam = sample_normal_foam(cascade, uv);
    vec4 detail_wave = sample_detail_wave(cascade, uv);
    vec3 spectral_displacement = displacement.xyz;
    spectral_displacement.xz *= mix(0.50, 1.0, clamp(ocean.detail_options.y, 0.0, 1.0));
    sample_value.displacement += spectral_displacement * displacement_weight;
    sample_value.displacement.y +=
        detail_wave.x * weight * cascade_geometry_detail_scale(cascade, camera_distance);
    float normal_detail = cascade_normal_detail_scale(cascade);
    vec3 spectral_normal = mix(vec3(0.0, 1.0, 0.0), normal_foam.xyz, normal_detail);
    vec3 detail_normal = ocean_normal_from_slope(detail_wave.yz);
    sample_value.normal_sum += mix(spectral_normal, detail_normal,
                                   cascade_geometry_detail_scale(cascade, camera_distance) * 0.42) *
                               weight;
    sample_value.foam =
        max(sample_value.foam, max(normal_foam.w, detail_wave.w) * displacement_weight);
    sample_value.weight_sum += weight;
}

void add_disturbance(inout SurfaceSample sample_value, vec2 position) {
    float radius = max(ocean.disturbance.z, 0.001);
    float strength = max(ocean.disturbance.w, 0.0);
    if (strength <= 0.0001) {
        return;
    }
    vec2 delta = position - ocean.disturbance.xy;
    float distance_to_center = length(delta);
    float envelope = exp(-distance_to_center / radius);
    float phase = (distance_to_center * 0.42) - (ocean.camera_time.w * 5.8);
    float ripple = sin(phase) * envelope * strength;
    sample_value.displacement.y += ripple;
    sample_value.foam = max(sample_value.foam, smoothstep(0.04, 0.28, abs(ripple)));
}

float shoreline_mask(vec2 position) {
    float influence = clamp(ocean.shading_options.z, 0.0, 1.0);
    vec2 shoal_position = (position - vec2(-520.0, -240.0)) / vec2(1.85, 0.75);
    float shoal = 1.0 - smoothstep(140.0, 640.0, length(shoal_position));
    return shoal * influence;
}

SurfaceSample sample_ocean_once(vec2 position, float camera_distance) {
    SurfaceSample sample_value;
    sample_value.displacement = vec3(0.0);
    sample_value.normal_sum = vec3(0.0);
    sample_value.foam = 0.0;
    sample_value.weight_sum = 0.0;

    add_cascade(sample_value, 2u, position, camera_distance);
    add_cascade(sample_value, 1u, position, camera_distance);
    add_cascade(sample_value, 0u, position, camera_distance);
    if (sample_value.weight_sum > 0.0001) {
        sample_value.displacement /= sample_value.weight_sum;
        sample_value.normal_sum /= sample_value.weight_sum;
    }
    OceanMacroWaveSample macro_waves =
        ocean_macro_waves(position, ocean.camera_time.w * ocean.wave_options.z, ocean.wave_options.y,
                          ocean.wave_options.x, ocean.detail_options.x, ocean.wave_options.w);
    float normal_length = length(sample_value.normal_sum);
    vec2 slope = normal_length > 0.0001
                     ? ocean_slope_from_normal(sample_value.normal_sum / normal_length)
                     : vec2(0.0);
    sample_value.displacement += macro_waves.displacement;
    sample_value.normal_sum = ocean_normal_from_slope(slope + macro_waves.slope);
    sample_value.foam = max(sample_value.foam, macro_waves.foam * ocean.detail_options.z * 0.10);
    add_disturbance(sample_value, position);
    return sample_value;
}

SurfaceSample sample_ocean(vec2 position, float camera_distance) {
    SurfaceSample first = sample_ocean_once(position, camera_distance);
    vec2 refined_position = position + first.displacement.xz * 0.24;
    SurfaceSample refined = sample_ocean_once(refined_position, camera_distance);
    first.displacement = mix(first.displacement, refined.displacement, 0.14);
    first.normal_sum = normalize(mix(first.normal_sum, refined.normal_sum, 0.32));
    first.foam = max(first.foam, refined.foam);
    return first;
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
    float snap = max(patch_cell_size / exp2(ocean.debug_options.z), 0.001);
    vec2 snapped_center = floor(camera_xz / snap) * snap;
    vec2 patch_position = clipmap_patch_position(uv);
    float patch_alpha = clipmap_patch_alpha(patch_position, patch_cell_size);
    vec2 base_position_xz = snapped_center + patch_position;
    float camera_distance = length(base_position_xz - camera_xz);

    SurfaceSample ocean_sample = sample_ocean(base_position_xz, camera_distance);
    vec2 position_xz = base_position_xz + ocean_sample.displacement.xz;
    float shore = shoreline_mask(position_xz);
    float depth = mix(max(ocean.cascade_options.w, 0.1), 1.4, shore);
    float shore_foam = smoothstep(0.10, 0.72, shore) * (0.45 + 0.55 * sin(ocean.camera_time.w));

    float normal_length = length(ocean_sample.normal_sum);
    vec3 normal = normal_length > 0.0001 ? ocean_sample.normal_sum / normal_length
                                         : vec3(0.0, 1.0, 0.0);
    vec3 world_position = vec3(position_xz.x, ocean_sample.displacement.y, position_xz.y);

    frag_world_position = world_position;
    frag_normal = normal;
    frag_wave = vec4(ocean_sample.displacement.y,
                     clamp((ocean_sample.foam * 0.75) + shore_foam, 0.0, 1.0),
                     camera_distance, depth);
    frag_displacement = ocean_sample.displacement;
    frag_sample_position = base_position_xz;
    frag_barycentric = triangle_barycentric(vertex_in_cell);
    frag_patch_alpha = patch_alpha;
    gl_Position = ocean.view_projection * vec4(world_position, 1.0);
}
