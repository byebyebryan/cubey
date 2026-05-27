#version 450
#extension GL_GOOGLE_include_directive : require

#include "ocean_macro_waves.glsl"

layout(set = 0, binding = 0) uniform sampler2D displacement_near_texture;
layout(set = 0, binding = 1) uniform sampler2D displacement_mid_texture;
layout(set = 0, binding = 2) uniform sampler2D displacement_far_texture;
layout(set = 0, binding = 3) uniform sampler2D normal_foam_near_texture;
layout(set = 0, binding = 4) uniform sampler2D normal_foam_mid_texture;
layout(set = 0, binding = 5) uniform sampler2D normal_foam_far_texture;

layout(push_constant) uniform OceanParams {
    mat4 view_projection;
    vec4 camera_time;
    vec4 mesh_options;
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

vec2 projected_grid_position(vec2 signed_uv) {
    float radial = clamp(length(signed_uv) * 0.70710678, 0.0, 1.0);
    float scale = mix(0.22, 1.0, pow(radial, 1.35));
    return signed_uv * scale;
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
        return 1.0 - smoothstep(ocean.mesh_options.y * 0.18, ocean.mesh_options.y * 0.55,
                                camera_distance);
    }
    if (cascade == 1u) {
        float fade_in = smoothstep(ocean.mesh_options.y * 0.10, ocean.mesh_options.y * 0.22,
                                   camera_distance);
        float fade_out = 1.0 - smoothstep(ocean.mesh_options.y * 0.58,
                                          ocean.mesh_options.y * 0.94, camera_distance);
        return fade_in * fade_out;
    }
    return smoothstep(ocean.mesh_options.y * 0.34, ocean.mesh_options.y * 0.72, camera_distance);
}

float cascade_displacement_detail_scale(uint cascade) {
    float detail = clamp(ocean.detail_options.y, 0.0, 1.0);
    if (cascade == 0u) {
        return 0.45 * detail * detail;
    }
    if (cascade == 1u) {
        return mix(1.02, 1.24, detail);
    }
    return mix(1.72, 1.48, detail);
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

void add_cascade(inout SurfaceSample sample_value, uint cascade, vec2 position,
                 float camera_distance) {
    float patch_length = cascade_patch_length(cascade);
    float weight = cascade_weight(cascade, camera_distance);
    float displacement_weight = weight * cascade_displacement_detail_scale(cascade);
    vec2 uv = cascade_sample_position(cascade, position) / max(patch_length, 0.001);
    vec4 displacement = sample_displacement(cascade, uv);
    vec4 normal_foam = sample_normal_foam(cascade, uv);
    vec3 spectral_displacement = displacement.xyz;
    spectral_displacement.xz *= mix(0.50, 1.0, clamp(ocean.detail_options.y, 0.0, 1.0));
    sample_value.displacement += spectral_displacement * displacement_weight;
    float normal_detail = cascade_normal_detail_scale(cascade);
    sample_value.normal_sum += mix(vec3(0.0, 1.0, 0.0), normal_foam.xyz, normal_detail) * weight;
    sample_value.foam = max(sample_value.foam, max(displacement.w, normal_foam.w) *
                                                   displacement_weight);
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

SurfaceSample sample_ocean(vec2 position, float camera_distance) {
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
    sample_value.foam = max(sample_value.foam, macro_waves.foam * ocean.detail_options.z * 0.35);
    add_disturbance(sample_value, position);
    return sample_value;
}

void main() {
    uint cells = max(uint(ocean.mesh_options.x + 0.5), 1u);
    uint vertex_in_cell = uint(gl_VertexIndex) % 6u;
    uint cell_index = uint(gl_VertexIndex) / 6u;
    uint cell_x = cell_index % cells;
    uint cell_z = cell_index / cells;

    vec2 uv = (vec2(cell_x, cell_z) + triangle_corner(vertex_in_cell)) / float(cells);
    vec2 signed_uv = (uv * 2.0) - 1.0;
    vec2 projected_uv = projected_grid_position(signed_uv);

    vec2 camera_xz = ocean.camera_time.xz;
    float snap = max(ocean.mesh_options.z, 0.001);
    vec2 snapped_center = floor(camera_xz / snap) * snap;
    vec2 base_position_xz = snapped_center + (projected_uv * ocean.mesh_options.y);
    float camera_distance = length(base_position_xz - camera_xz);

    SurfaceSample ocean_sample = sample_ocean(base_position_xz, camera_distance);
    vec2 position_xz = base_position_xz + ocean_sample.displacement.xz;
    float shore = shoreline_mask(position_xz);
    float depth = mix(82.0, 1.4, shore);
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
    gl_Position = ocean.view_projection * vec4(world_position, 1.0);
}
