#version 450
#extension GL_GOOGLE_include_directive : require

#include "cubey/color_space.glsl"
#include "cubey/pbr.glsl"
#include "ocean_atmosphere.glsl"
#include "ocean_macro_waves.glsl"

layout(set = 0, binding = 0) uniform sampler2D displacement_near_texture;
layout(set = 0, binding = 1) uniform sampler2D displacement_mid_texture;
layout(set = 0, binding = 2) uniform sampler2D displacement_far_texture;
layout(set = 0, binding = 3) uniform sampler2D normal_foam_near_texture;
layout(set = 0, binding = 4) uniform sampler2D normal_foam_mid_texture;
layout(set = 0, binding = 5) uniform sampler2D normal_foam_far_texture;
layout(set = 0, binding = 6) uniform sampler2D foam_history_a_near_texture;
layout(set = 0, binding = 7) uniform sampler2D foam_history_a_mid_texture;
layout(set = 0, binding = 8) uniform sampler2D foam_history_a_far_texture;
layout(set = 0, binding = 9) uniform sampler2D foam_history_b_near_texture;
layout(set = 0, binding = 10) uniform sampler2D foam_history_b_mid_texture;
layout(set = 0, binding = 11) uniform sampler2D foam_history_b_far_texture;
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

layout(location = 0) in vec3 frag_world_position;
layout(location = 1) in vec3 frag_normal;
layout(location = 2) in vec4 frag_wave;
layout(location = 3) in vec3 frag_displacement;
layout(location = 4) in vec2 frag_sample_position;
noperspective layout(location = 5) in vec3 frag_barycentric;
layout(location = 6) in float frag_patch_alpha;

layout(location = 0) out vec4 out_color;

const uint OCEAN_VIEW_FINAL = 0u;
const uint OCEAN_VIEW_HEIGHT = 1u;
const uint OCEAN_VIEW_DISPLACEMENT = 2u;
const uint OCEAN_VIEW_NORMAL = 3u;
const uint OCEAN_VIEW_FOAM = 4u;
const uint OCEAN_VIEW_DETAIL = 5u;
const uint OCEAN_VIEW_REFLECTION = 6u;
const uint OCEAN_VIEW_REFRACTION = 7u;
const uint OCEAN_VIEW_SPECTRUM = 8u;
const uint OCEAN_VIEW_WIREFRAME = 9u;

struct FragmentSurfaceSample {
    vec3 displacement;
    vec3 normal_sum;
    vec2 detail_slope_sum;
    float detail_height;
    float foam;
    float detail_foam;
    vec4 persistent_foam;
    float weight_sum;
    float normal_weight_sum;
    float detail_weight_sum;
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

vec4 sample_detail_wave(uint cascade, vec2 uv) {
    if (cascade == 0u) {
        return texture(detail_wave_near_texture, uv);
    }
    if (cascade == 1u) {
        return texture(detail_wave_mid_texture, uv);
    }
    return texture(detail_wave_far_texture, uv);
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

vec4 sample_foam_history(uint cascade, vec2 uv) {
    bool use_b = ocean.debug_options.y > 0.5;
    if (cascade == 0u) {
        return use_b ? texture(foam_history_b_near_texture, uv)
                     : texture(foam_history_a_near_texture, uv);
    }
    if (cascade == 1u) {
        return use_b ? texture(foam_history_b_mid_texture, uv)
                     : texture(foam_history_a_mid_texture, uv);
    }
    return use_b ? texture(foam_history_b_far_texture, uv)
                 : texture(foam_history_a_far_texture, uv);
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
        return 1.0 - smoothstep(ocean.mesh_options.z * 0.18, ocean.mesh_options.z * 0.55,
                                camera_distance);
    }
    if (cascade == 1u) {
        float fade_in = smoothstep(ocean.mesh_options.z * 0.10, ocean.mesh_options.z * 0.22,
                                   camera_distance);
        float fade_out = 1.0 - smoothstep(ocean.mesh_options.z * 0.58,
                                          ocean.mesh_options.z * 0.94, camera_distance);
        return fade_in * fade_out;
    }
    return smoothstep(ocean.mesh_options.z * 0.34, ocean.mesh_options.z * 0.72, camera_distance);
}

float cascade_detail_filter(float camera_distance) {
    float near_fade =
        1.0 - smoothstep(ocean.mesh_options.z * 0.68, ocean.mesh_options.z * 1.10,
                         camera_distance);
    return clamp(mix(0.42, 1.0, near_fade), 0.0, 1.0);
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

void add_cascade(inout FragmentSurfaceSample sample_value, uint cascade, vec2 position,
                 float camera_distance) {
    float patch_length = cascade_patch_length(cascade);
    float weight = cascade_weight(cascade, camera_distance);
    float displacement_weight = weight * cascade_displacement_detail_scale(cascade);
    vec2 uv = cascade_sample_position(cascade, position) / max(patch_length, 0.001);
    vec4 displacement = sample_displacement(cascade, uv);
    vec4 normal_foam = sample_normal_foam(cascade, uv);
    vec4 detail_wave = sample_detail_wave(cascade, uv);
    float detail_filter = cascade_detail_filter(camera_distance);
    float detail_strength = clamp(ocean.detail_options.y, 0.0, 1.0);
    float normal_weight = weight * detail_filter * detail_strength;
    float detail_weight = normal_weight * smoothstep(0.04, 0.38, ocean.detail_wave_options.x);
    vec3 spectral_displacement = displacement.xyz;
    spectral_displacement.xz *= mix(0.50, 1.0, detail_strength);
    sample_value.displacement += spectral_displacement * displacement_weight;
    sample_value.detail_height += detail_wave.x * detail_weight;
    sample_value.normal_sum += normal_foam.xyz * normal_weight;
    sample_value.detail_slope_sum += detail_wave.yz * detail_weight;
    sample_value.foam =
        max(sample_value.foam,
            max(displacement.w, normal_foam.w) * normal_weight *
                cascade_displacement_detail_scale(cascade));
    sample_value.detail_foam = max(sample_value.detail_foam, detail_wave.w * detail_weight);
    sample_value.persistent_foam =
        max(sample_value.persistent_foam, sample_foam_history(cascade, uv) * weight);
    sample_value.weight_sum += weight;
    sample_value.normal_weight_sum += normal_weight;
    sample_value.detail_weight_sum += detail_weight;
}

FragmentSurfaceSample sample_fragment_surface(vec2 position, float camera_distance) {
    FragmentSurfaceSample sample_value;
    sample_value.displacement = vec3(0.0);
    sample_value.normal_sum = vec3(0.0);
    sample_value.detail_slope_sum = vec2(0.0);
    sample_value.detail_height = 0.0;
    sample_value.foam = 0.0;
    sample_value.detail_foam = 0.0;
    sample_value.persistent_foam = vec4(0.0);
    sample_value.weight_sum = 0.0;
    sample_value.normal_weight_sum = 0.0;
    sample_value.detail_weight_sum = 0.0;
    add_cascade(sample_value, 2u, position, camera_distance);
    add_cascade(sample_value, 1u, position, camera_distance);
    add_cascade(sample_value, 0u, position, camera_distance);
    if (sample_value.weight_sum > 0.0001) {
        sample_value.displacement /= sample_value.weight_sum;
    }
    if (sample_value.normal_weight_sum > 0.0001) {
        sample_value.normal_sum /= sample_value.normal_weight_sum;
    }
    if (sample_value.detail_weight_sum > 0.0001) {
        sample_value.detail_slope_sum /= sample_value.detail_weight_sum;
        sample_value.detail_height /= sample_value.detail_weight_sum;
    }
    sample_value.displacement.y += sample_value.detail_height;
    OceanMacroWaveSample macro_waves =
        ocean_macro_waves(position, ocean.camera_time.w * ocean.wave_options.z, ocean.wave_options.y,
                          ocean.wave_options.x, ocean.detail_options.x, ocean.wave_options.w);
    float normal_length = length(sample_value.normal_sum);
    vec2 slope = normal_length > 0.0001
                     ? ocean_slope_from_normal(sample_value.normal_sum / normal_length)
                     : vec2(0.0);
    sample_value.displacement += macro_waves.displacement;
    vec2 detail_slope = sample_value.detail_slope_sum;
    sample_value.normal_sum = ocean_normal_from_slope(slope + macro_waves.slope +
                                                      detail_slope * 0.44);
    sample_value.foam = max(sample_value.foam, macro_waves.foam * ocean.detail_options.z * 0.35);
    sample_value.detail_foam = max(sample_value.detail_foam, macro_waves.foam * 0.16);
    sample_value.persistent_foam =
        max(sample_value.persistent_foam, vec4(macro_waves.foam * 0.20, 0.0, 0.0, 0.0));
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

float foam_mask(vec3 normal, float depth) {
    float crest = frag_wave.y;
    float shallow_foam = (1.0 - smoothstep(1.5, 11.0, depth)) * ocean.shading_options.z;
    return clamp((crest + shallow_foam * 0.42) * ocean.detail_options.z, 0.0, 1.0);
}

float foam_breakup(vec2 position) {
    float slow = value_noise(position * 0.035 +
                            vec2(ocean.camera_time.w * 0.020, ocean.camera_time.w * -0.014));
    float fine = value_noise(position * 0.115 +
                            vec2(ocean.camera_time.w * -0.035, ocean.camera_time.w * 0.018));
    float breakup = clamp(ocean.detail_wave_options.z, 0.0, 2.0);
    return mix(1.0, mix(0.80, 1.10, slow) * mix(0.86, 1.06, fine), breakup);
}

vec3 debug_height_color(float height) {
    float value = clamp(height * 0.06 + 0.5, 0.0, 1.0);
    vec3 low = cubey_srgb_to_linear(vec3(0.04, 0.18, 0.42));
    vec3 mid = cubey_srgb_to_linear(vec3(0.12, 0.65, 0.78));
    vec3 high = cubey_srgb_to_linear(vec3(0.96, 0.94, 0.78));
    return value < 0.5 ? mix(low, mid, value * 2.0) : mix(mid, high, (value - 0.5) * 2.0);
}

float wireframe_line(vec3 barycentric) {
    vec3 width = max(fwidth(barycentric), vec3(0.0001));
    vec3 edge = smoothstep(vec3(0.0), width * 1.35, barycentric);
    return 1.0 - min(min(edge.x, edge.y), edge.z);
}

vec3 wireframe_lod_tint() {
    float phase = fract(ocean.debug_options.z * 0.173) * 6.2831853;
    vec3 tint = 0.55 + 0.45 * cos(vec3(0.0, 2.0943951, 4.1887902) + phase);
    return cubey_srgb_to_linear(tint);
}

vec3 apply_display(vec3 color) {
    return cubey_pbr_apply_display_transform(color, ocean.display_transform);
}

void main() {
    uint view = uint(ocean.debug_options.x + 0.5);
    vec3 camera_position = ocean.camera_time.xyz;
    FragmentSurfaceSample surface_sample =
        sample_fragment_surface(frag_sample_position, frag_wave.z);
    float refinement_weight = 0.22;
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
    vec3 normal = normalize(mix(normalize(frag_normal), sampled_normal,
                                clamp(ocean.detail_options.y * 0.58, 0.0, 1.0)));
    float depth = max(frag_wave.w, 0.0);
    float foam = foam_mask(normal, depth);
    foam = clamp(max(max(foam, surface_sample.foam * ocean.detail_options.z),
                     max(surface_sample.detail_foam * ocean.detail_options.z,
                         surface_sample.persistent_foam.x * ocean.detail_options.z)),
                 0.0, 1.0);
    float shaded_foam = clamp(foam * foam_breakup(refined_sample_position), 0.0, 1.0);
    float foam_freshness =
        clamp(max(surface_sample.detail_foam, surface_sample.persistent_foam.y), 0.0, 1.0);

    vec3 reflection_dir = reflect(-view_dir, normal);
    vec3 reflection = ocean_sky_color(reflection_dir);
    vec3 refraction = refraction_color(depth, foam);
    float fresnel = 0.020 + 0.980 * pow(clamp(1.0 - dot(normal, view_dir), 0.0, 1.0), 5.0);
    vec3 water = mix(refraction, reflection, fresnel);
    float sun_alignment = max(dot(reflection_dir, ocean_sun_direction()), 0.0);
    float grazing = pow(clamp(1.0 - dot(normal, view_dir), 0.0, 1.0), 0.55);
    float detail_strength = clamp(ocean.detail_options.y, 0.0, 1.0);
    float normal_variation = length(dFdx(normal)) + length(dFdy(normal));
    float specular_filter = 1.0 / (1.0 + normal_variation * 95.0);
    float broad_glint = pow(sun_alignment, mix(12.0, 38.0, detail_strength)) * 0.24;
    float sharp_glint = pow(sun_alignment, mix(70.0, 150.0, detail_strength)) * detail_strength *
                        detail_strength * 0.30 * specular_filter;
    float sun_glint = broad_glint + sharp_glint;
    sun_glint *= 1.0 - shaded_foam * mix(0.48, 0.74, foam_freshness);
    water += cubey_srgb_to_linear(vec3(1.0, 0.78, 0.46)) * sun_glint * (0.14 + grazing * 0.58);
    water = mix(water, refraction, shaded_foam * 0.14);
    vec3 old_foam = cubey_srgb_to_linear(vec3(0.68, 0.84, 0.82));
    vec3 fresh_foam = cubey_srgb_to_linear(vec3(0.92, 0.97, 0.91));
    vec3 foam_color = mix(old_foam, fresh_foam, foam_freshness);
    water = mix(water, foam_color, shaded_foam);

    float horizon_fog = smoothstep(ocean.mesh_options.z * 0.28, ocean.mesh_options.z * 0.92,
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
        color = cubey_srgb_to_linear(vec3(shaded_foam, foam_freshness,
                                          clamp(surface_sample.persistent_foam.z, 0.0, 1.0)));
    } else if (view == OCEAN_VIEW_DETAIL) {
        vec3 detail_normal = ocean_normal_from_slope(surface_sample.detail_slope_sum);
        vec3 height_color = debug_height_color(surface_sample.detail_height);
        color = mix(detail_normal * 0.5 + 0.5, height_color, 0.42);
        color = mix(color, cubey_srgb_to_linear(vec3(0.92, 0.96, 0.88)),
                    clamp(surface_sample.detail_foam, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_REFLECTION) {
        color = reflection;
    } else if (view == OCEAN_VIEW_REFRACTION) {
        color = refraction;
    } else if (view == OCEAN_VIEW_SPECTRUM) {
        color = mix(debug_height_color(frag_wave.x), cubey_srgb_to_linear(vec3(0.92, 0.96, 0.90)),
                    clamp(shaded_foam, 0.0, 1.0));
    } else if (view == OCEAN_VIEW_WIREFRAME) {
        float line = wireframe_line(frag_barycentric);
        vec3 base = mix(cubey_srgb_to_linear(vec3(0.015, 0.035, 0.045)),
                        debug_height_color(frag_wave.x), 0.18);
        vec3 lod_tint = wireframe_lod_tint();
        base = mix(base, lod_tint, 0.10);
        vec3 wire = mix(cubey_srgb_to_linear(vec3(0.82, 0.94, 1.0)), lod_tint, 0.18);
        color = mix(base, wire, line);
    }

    out_color = vec4(apply_display(color), clamp(frag_patch_alpha, 0.0, 1.0));
}
