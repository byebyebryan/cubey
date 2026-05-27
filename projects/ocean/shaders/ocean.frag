#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/pbr.glsl"
#include "ocean_atmosphere.glsl"

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
} ocean;

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec4 frag_wave;
layout(location = 3) in vec3 frag_displacement;
layout(location = 4) in vec2 frag_sample_position;

layout(location = 0) out vec4 out_color;

const uint OCEAN_VIEW_FINAL = 0u;
const uint OCEAN_VIEW_HEIGHT = 1u;
const uint OCEAN_VIEW_DISPLACEMENT = 2u;
const uint OCEAN_VIEW_NORMAL = 3u;
const uint OCEAN_VIEW_FOAM = 4u;
const uint OCEAN_VIEW_REFLECTION = 5u;
const uint OCEAN_VIEW_REFRACTION = 6u;
const uint OCEAN_VIEW_SPECTRUM = 7u;

struct FragmentSurfaceSample {
    vec3 displacement;
    vec3 normal_sum;
    float foam;
    float weight_sum;
    float normal_weight_sum;
};

float hash21(vec2 value) {
    vec3 p = fract(vec3(value.xyx) * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float value_noise(vec2 value) {
    vec2 cell = floor(value);
    vec2 local = fract(value);
    vec2 smooth_local = local * local * (3.0 - 2.0 * local);
    float a = hash21(cell);
    float b = hash21(cell + vec2(1.0, 0.0));
    float c = hash21(cell + vec2(0.0, 1.0));
    float d = hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, smooth_local.x), mix(c, d, smooth_local.x), smooth_local.y);
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

vec4 sample_displacement(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(displacement_near_texture, uv);
    }
    if (cascade == 1u) {
        return texture(displacement_mid_texture, uv);
    }
    return texture(displacement_far_texture, uv);
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

float uv_texel_footprint(vec2 uv) {
    float resolution = max(ocean.spectrum_options.x, 1.0);
    vec2 dx = dFdx(uv) * resolution;
    vec2 dy = dFdy(uv) * resolution;
    return max(length(dx), length(dy));
}

float cascade_detail_filter(vec2 uv, float camera_distance) {
    float derivative_filter = 1.0 - smoothstep(0.85, 3.2, uv_texel_footprint(uv));
    float horizon_filter =
        1.0 - smoothstep(ocean.mesh_options.y * 0.26, ocean.mesh_options.y * 0.84,
                         camera_distance);
    return clamp(max(derivative_filter * 0.75, horizon_filter * 0.35), 0.0, 1.0);
}

void add_cascade(inout FragmentSurfaceSample sample_value, uint cascade, vec2 position,
                 float camera_distance) {
    float patch_length = cascade_patch_length(cascade);
    float weight = cascade_weight(cascade, camera_distance);
    vec2 uv = position / max(patch_length, 0.001);
    vec4 displacement = sample_displacement(cascade, uv);
    vec4 normal_foam = sample_normal_foam(cascade, uv);
    float detail_filter = cascade_detail_filter(uv, camera_distance);
    float normal_weight = weight * detail_filter;
    sample_value.displacement += displacement.xyz * weight;
    sample_value.normal_sum += normal_foam.xyz * normal_weight;
    sample_value.foam =
        max(sample_value.foam, max(displacement.w, normal_foam.w) * normal_weight);
    sample_value.weight_sum += weight;
    sample_value.normal_weight_sum += normal_weight;
}

FragmentSurfaceSample sample_fragment_surface(vec2 position, float camera_distance) {
    FragmentSurfaceSample sample_value;
    sample_value.displacement = vec3(0.0);
    sample_value.normal_sum = vec3(0.0);
    sample_value.foam = 0.0;
    sample_value.weight_sum = 0.0;
    sample_value.normal_weight_sum = 0.0;
    add_cascade(sample_value, 2u, position, camera_distance);
    add_cascade(sample_value, 1u, position, camera_distance);
    add_cascade(sample_value, 0u, position, camera_distance);
    if (sample_value.weight_sum > 0.0001) {
        sample_value.displacement /= sample_value.weight_sum;
    }
    if (sample_value.normal_weight_sum > 0.0001) {
        sample_value.normal_sum /= sample_value.normal_weight_sum;
    }
    return sample_value;
}

vec3 refraction_color(float depth, float foam) {
    vec3 shallow_water = cubey_srgb_to_linear(vec3(0.17, 0.58, 0.70));
    vec3 mid_water = cubey_srgb_to_linear(vec3(0.030, 0.22, 0.34));
    vec3 deep_water = cubey_srgb_to_linear(vec3(0.006, 0.035, 0.085));
    vec3 sand = cubey_srgb_to_linear(vec3(0.52, 0.51, 0.42));
    float absorption = max(ocean.shading_options.x, 0.0);
    float transmittance = exp(-absorption * depth);
    vec3 water_tint = mix(deep_water, mid_water, clamp(transmittance * 2.2, 0.0, 1.0));
    water_tint = mix(water_tint, shallow_water, clamp(transmittance * 0.72, 0.0, 1.0));
    vec3 bottom = mix(water_tint, sand, clamp(transmittance * 0.38, 0.0, 1.0));
    return mix(bottom, cubey_srgb_to_linear(vec3(0.74, 0.90, 0.90)), foam * 0.10);
}

vec2 rotated_unit(vec2 value, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return normalize(vec2(value.x * c - value.y * s, value.x * s + value.y * c));
}

vec2 add_detail_wave(vec2 slope, vec2 position, vec2 direction, float wavelength,
                     float slope_amplitude, float speed, float phase) {
    float frequency = 6.2831853 / max(wavelength, 0.001);
    float theta = dot(position, direction) * frequency + ocean.camera_time.w * speed + phase;
    return slope + direction * (cos(theta) * slope_amplitude);
}

vec2 procedural_detail_slope(vec2 position, float camera_distance) {
    float near_fade =
        1.0 - smoothstep(ocean.mesh_options.y * 0.025, ocean.mesh_options.y * 0.34,
                         camera_distance);
    float strength = ocean.detail_options.y * near_fade;
    if (strength <= 0.0001) {
        return vec2(0.0);
    }

    vec2 wind = vec2(cos(ocean.wave_options.y), sin(ocean.wave_options.y));
    vec2 slope = vec2(0.0);
    slope = add_detail_wave(slope, position, rotated_unit(wind, 0.28), 18.0, 0.018, 0.95, 0.4);
    slope = add_detail_wave(slope, position, rotated_unit(wind, -0.52), 9.0, 0.014, 1.7, 1.3);
    slope = add_detail_wave(slope, position, rotated_unit(wind, 1.18), 4.5, 0.008, 2.6, 2.7);
    slope = add_detail_wave(slope, position, rotated_unit(wind, -1.38), 2.4, 0.004, 3.8, 4.2);
    return slope * strength;
}

vec3 normal_from_slope(vec2 slope) {
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

vec3 add_procedural_detail(vec3 normal, vec2 position, float camera_distance) {
    vec2 base_slope = -normal.xz / max(normal.y, 0.22);
    vec2 detail_slope = procedural_detail_slope(position, camera_distance);
    return normal_from_slope(base_slope + detail_slope);
}

float foam_mask(vec3 normal, float depth) {
    float crest = frag_wave.y;
    float slope = 1.0 - clamp(normal.y, 0.0, 1.0);
    float slope_foam = smoothstep(ocean.detail_options.w, ocean.detail_options.w + 0.35,
                                  slope * ocean.wave_options.w * 4.5);
    float shallow_foam = (1.0 - smoothstep(1.5, 11.0, depth)) * ocean.shading_options.z;
    float noisy_breakup =
        mix(0.72, 1.06,
            value_noise(frag_world_position.xz * 0.026 +
                        vec2(ocean.camera_time.w * 0.018, ocean.camera_time.w * -0.011)));
    return clamp((crest + slope_foam * 0.32 + shallow_foam * 0.48) *
                     ocean.detail_options.z * noisy_breakup,
                 0.0, 1.0);
}

vec3 debug_height_color(float height) {
    float value = clamp(height * 0.06 + 0.5, 0.0, 1.0);
    vec3 low = cubey_srgb_to_linear(vec3(0.04, 0.18, 0.42));
    vec3 mid = cubey_srgb_to_linear(vec3(0.12, 0.65, 0.78));
    vec3 high = cubey_srgb_to_linear(vec3(0.96, 0.94, 0.78));
    return value < 0.5 ? mix(low, mid, value * 2.0) : mix(mid, high, (value - 0.5) * 2.0);
}

vec3 apply_display(vec3 color) {
    return cubey_pbr_apply_display_transform(color, ocean.display_transform);
}

void main() {
    uint view = uint(ocean.debug_options.x + 0.5);
    vec3 camera_position = ocean.camera_time.xyz;
    FragmentSurfaceSample surface_sample =
        sample_fragment_surface(frag_sample_position, frag_wave.z);
    float refinement_weight =
        1.0 - smoothstep(ocean.mesh_options.y * 0.08, ocean.mesh_options.y * 0.24, frag_wave.z);
    vec2 refined_sample_position =
        mix(frag_sample_position, frag_sample_position + surface_sample.displacement.xz,
            refinement_weight);
    vec3 refined_world_position =
        mix(frag_world_position,
            vec3(refined_sample_position.x, surface_sample.displacement.y,
                 refined_sample_position.y),
            refinement_weight);
    vec3 view_dir = normalize(camera_position - refined_world_position);
    vec3 sampled_normal = length(surface_sample.normal_sum) > 0.0001
                              ? normalize(surface_sample.normal_sum)
                              : normalize(frag_normal);
    float normal_detail_fade =
        1.0 - smoothstep(ocean.mesh_options.y * 0.10, ocean.mesh_options.y * 0.56, frag_wave.z);
    vec3 normal = normalize(mix(normalize(frag_normal), sampled_normal,
                                clamp(ocean.detail_options.y * 0.62 * normal_detail_fade, 0.0,
                                      1.0)));
    normal = add_procedural_detail(normal, refined_sample_position, frag_wave.z);
    float depth = max(frag_wave.w, 0.0);
    float foam = foam_mask(normal, depth);
    foam = clamp(max(foam, surface_sample.foam * ocean.detail_options.z), 0.0, 1.0);

    vec3 reflection_dir = reflect(-view_dir, normal);
    vec3 reflection = ocean_sky_color(reflection_dir);
    vec3 refraction = refraction_color(depth, foam);
    float fresnel = 0.020 + 0.980 * pow(clamp(1.0 - dot(normal, view_dir), 0.0, 1.0), 5.0);
    vec3 water = mix(refraction, reflection, fresnel);
    float sun_alignment = max(dot(reflection_dir, ocean_sun_direction()), 0.0);
    float grazing = pow(clamp(1.0 - dot(normal, view_dir), 0.0, 1.0), 0.55);
    float sun_glint = pow(sun_alignment, 120.0) * 0.65 + pow(sun_alignment, 620.0) * 2.0;
    water += cubey_srgb_to_linear(vec3(1.0, 0.78, 0.46)) * sun_glint * (0.18 + grazing);
    vec3 foam_color = cubey_srgb_to_linear(vec3(0.82, 0.94, 0.91));
    water = mix(water, foam_color, foam);

    float horizon_fog = smoothstep(ocean.mesh_options.y * 0.28, ocean.mesh_options.y * 0.92,
                                   frag_wave.z) *
                        ocean.mesh_options.w;
    vec3 horizon_dir = normalize(vec3(-view_dir.x, 0.055, -view_dir.z));
    vec3 color = mix(water, ocean_sky_color(horizon_dir), horizon_fog);

    if (view == OCEAN_VIEW_HEIGHT) {
        color = debug_height_color(frag_wave.x);
    } else if (view == OCEAN_VIEW_DISPLACEMENT) {
        color = cubey_srgb_to_linear(
            clamp(abs(frag_displacement) * vec3(0.08, 0.06, 0.08), vec3(0.0), vec3(1.0)));
    } else if (view == OCEAN_VIEW_NORMAL) {
        color = normal * 0.5 + 0.5;
    } else if (view == OCEAN_VIEW_FOAM) {
        color = cubey_srgb_to_linear(vec3(foam));
    } else if (view == OCEAN_VIEW_REFLECTION) {
        color = reflection;
    } else if (view == OCEAN_VIEW_REFRACTION) {
        color = refraction;
    } else if (view == OCEAN_VIEW_SPECTRUM) {
        color = mix(debug_height_color(frag_wave.x), cubey_srgb_to_linear(vec3(0.92, 0.96, 0.90)),
                    clamp(foam, 0.0, 1.0));
    }

    out_color = vec4(apply_display(color), 1.0);
}
