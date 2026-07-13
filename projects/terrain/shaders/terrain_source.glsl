#ifndef CUBEY_TERRAIN_SOURCE_GLSL
#define CUBEY_TERRAIN_SOURCE_GLSL

#include "cubey/procedural/fastnoise_lite.glsl"
#include "cubey/procedural/noise.glsl"

struct TerrainSourceGpuBandParameters {
    vec4 shape;
    ivec4 control;
};

struct TerrainSourceGpuParameters {
    TerrainSourceGpuBandParameters macro;
    TerrainSourceGpuBandParameters structure;
    TerrainSourceGpuBandParameters detail;
    vec4 composition;
    vec4 elevation;
    vec4 weathering;
    TerrainSourceGpuBandParameters v3_warp;
    TerrainSourceGpuBandParameters v3_range;
    TerrainSourceGpuBandParameters v3_massif;
    TerrainSourceGpuBandParameters v3_ridge;
    TerrainSourceGpuBandParameters v3_meso;
    vec4 v3_composition_0;
    vec4 v3_composition_1;
    ivec4 source_control;
};

struct TerrainSourceComponents {
    float range_support;
    float massif_height_m;
    float valley_delta_m;
    float ridge_delta_m;
    float meso_delta_m;
    float base_height_m;
};

struct TerrainSourceSample {
    float base_height_m;
    float height_m;
    vec2 gradient_xz;
    float weathering_delta_m;
};

// CPU parity constant: approximate mean of 1 - abs(OpenSimplex2S).
const float terrain_source_ridge_neutral = 0.65;

float terrain_source_octave_footprint_weight(float frequency, float footprint_m) {
    if (footprint_m <= 0.0) {
        return 1.0;
    }
    float wavelength_m = 1.0 / frequency;
    return 1.0 - smoothstep(wavelength_m * 0.25, wavelength_m * 0.5, footprint_m);
}

float terrain_source_band(TerrainSourceGpuBandParameters band, vec2 world_xz,
        float footprint_m) {
    fnl_state noise = fnlCreateState(band.control.x);
    noise.frequency = 1.0;
    noise.noise_type = FNL_NOISE_OPENSIMPLEX2S;
    noise.fractal_type = FNL_FRACTAL_NONE;

    vec2 rotated = vec2(0.8 * world_xz.x - 0.6 * world_xz.y,
        0.6 * world_xz.x + 0.8 * world_xz.y);
    float frequency = band.shape.x;
    float amplitude = 1.0;
    float value = 0.0;
    float weight = 0.0;
    for (int octave = 0; octave < 12; ++octave) {
        if (octave >= band.control.y) {
            break;
        }
        float footprint_weight =
            terrain_source_octave_footprint_weight(frequency, footprint_m);
        float neutral = mix(0.5, terrain_source_ridge_neutral, band.shape.w);
        float shaped = neutral;
        if (footprint_weight > 0.0) {
            noise.seed = band.control.x + octave * 1013;
            float octave_f = float(octave);
            float sample_value = fnlGetNoise2D(noise,
                rotated.x * frequency + octave_f * 17.31,
                rotated.y * frequency - octave_f * 9.17);
            float unit_value = sample_value * 0.5 + 0.5;
            float ridge = 1.0 - abs(sample_value);
            shaped = mix(unit_value, ridge, band.shape.w);
        }
        value += mix(neutral, shaped, footprint_weight) * amplitude;
        weight += amplitude;
        frequency *= band.shape.y;
        amplitude *= band.shape.z;
    }
    return weight > 0.0 ? value / weight : 0.5;
}

float terrain_source_v3_signed_band(TerrainSourceGpuBandParameters band, vec2 world_xz,
        float footprint_m) {
    vec2 octave_position = vec2(0.8 * world_xz.x - 0.6 * world_xz.y,
        0.6 * world_xz.x + 0.8 * world_xz.y);
    float frequency = band.shape.x;
    float amplitude = 1.0;
    float value = 0.0;
    float weight = 0.0;
    for (int octave = 0; octave < 8; ++octave) {
        if (octave >= band.control.y) {
            break;
        }
        float footprint_weight = terrain_source_octave_footprint_weight(frequency, footprint_m);
        float octave_f = float(octave);
        float sample_value = cubey_proc_gradient_noise_3d(vec3(
            octave_position.x * frequency + octave_f * 17.31,
            octave_position.y * frequency - octave_f * 9.17,
            octave_f * 0.713 + 0.37), uint(band.control.x + octave * 1013));
        value += sample_value * footprint_weight * amplitude;
        weight += amplitude;
        octave_position = vec2(
            0.8 * octave_position.x - 0.6 * octave_position.y,
            0.6 * octave_position.x + 0.8 * octave_position.y);
        frequency *= band.shape.y;
        amplitude *= band.shape.z;
    }
    return weight > 0.0 ? value / weight : 0.0;
}

TerrainSourceComponents terrain_source_v3_components(TerrainSourceGpuParameters parameters,
        vec2 world_xz, float footprint_m) {
    TerrainSourceGpuBandParameters warp_z_band = parameters.v3_warp;
    warp_z_band.control.x += 7919;
    float warp_x = terrain_source_v3_signed_band(parameters.v3_warp,
        world_xz + vec2(12031.0, -4507.0), footprint_m);
    float warp_z = terrain_source_v3_signed_band(warp_z_band,
        world_xz + vec2(-8213.0, 15119.0), footprint_m);
    vec2 warped = world_xz + vec2(warp_x, warp_z) * parameters.v3_composition_0.x;

    float range_noise = terrain_source_v3_signed_band(parameters.v3_range, warped, footprint_m);
    float range_support = smoothstep(-0.32, 0.42, range_noise);
    float massif_noise = terrain_source_v3_signed_band(parameters.v3_massif,
        warped + vec2(3107.0, -1903.0), footprint_m);
    float massif_unit = clamp(massif_noise * 0.5 + 0.5, 0.0, 1.0);
    float massif_shape = 0.20 + 0.80 * smoothstep(0.28, 0.74, massif_unit);
    float macro_profile = pow(range_support, 1.45) * massif_shape;
    float massif_height_m = parameters.elevation.x + parameters.elevation.y *
        pow(macro_profile, parameters.elevation.z);

    float valley_gate = 1.0 - smoothstep(0.26, 0.48, massif_unit);
    float valley_delta_m = -min(massif_height_m * parameters.v3_composition_0.y,
        parameters.v3_composition_0.z) * valley_gate * range_support;

    float ridge_field = terrain_source_v3_signed_band(parameters.v3_ridge,
        warped + vec2(-2411.0, 5327.0), footprint_m);
    float ridge_signal = clamp(1.0 - abs(ridge_field), 0.0, 1.0);
    float ridge_body = ridge_signal * ridge_signal * ridge_signal * ridge_signal;
    float highland_gate = smoothstep(0.10, 0.58, macro_profile);
    float ridge_delta_m = min(massif_height_m * parameters.v3_composition_0.w,
        parameters.v3_composition_1.x) * ridge_body * highland_gate;

    float meso_field = terrain_source_v3_signed_band(parameters.v3_meso,
        warped + vec2(1127.0, 2813.0), footprint_m);
    float face_gate = smoothstep(0.10, 0.48, macro_profile) *
        (1.0 - smoothstep(0.78, 0.96, macro_profile));
    float meso_delta_m = meso_field *
        min(massif_height_m * parameters.v3_composition_1.y,
            parameters.v3_composition_1.z) * face_gate;
    float base_height_m = max(
        massif_height_m + valley_delta_m + ridge_delta_m + meso_delta_m, 0.0);
    return TerrainSourceComponents(range_support, massif_height_m, valley_delta_m,
        ridge_delta_m, meso_delta_m, base_height_m);
}

float terrain_source_base_height(TerrainSourceGpuParameters parameters, vec2 world_xz,
        float footprint_m) {
    if (parameters.source_control.x == 2) {
        return terrain_source_v3_components(parameters, world_xz, footprint_m).base_height_m;
    }
    float macro_value = terrain_source_band(parameters.macro, world_xz, footprint_m);
    float structure_value = terrain_source_band(parameters.structure, world_xz, footprint_m);
    float detail_value = terrain_source_band(parameters.detail, world_xz, footprint_m);
    float mass = smoothstep(0.18, 0.82, macro_value);
    float structured = structure_value * (0.30 + 0.70 * mass);
    float local_detail = (detail_value - 0.5) * (0.15 + 0.85 * mass);
    float composed = clamp(parameters.composition.x * macro_value +
        parameters.composition.y * structured + parameters.composition.z * local_detail +
        parameters.composition.w, 0.0, 1.0);
    return parameters.elevation.x +
        parameters.elevation.y * pow(composed, parameters.elevation.z);
}

float terrain_source_weathering_delta(TerrainSourceGpuParameters parameters, vec2 world_xz,
        float footprint_m, float center_height_m) {
    if (parameters.weathering.w <= 0.5 || parameters.weathering.z <= 0.0 ||
            footprint_m >= parameters.weathering.x * 0.75) {
        return 0.0;
    }

    const float diagonal = 0.70710678118;
    const vec2 directions[8] = vec2[8](
        vec2(1.0, 0.0), vec2(diagonal, diagonal), vec2(0.0, 1.0),
        vec2(-diagonal, diagonal), vec2(-1.0, 0.0), vec2(-diagonal, -diagonal),
        vec2(0.0, -1.0), vec2(diagonal, -diagonal));
    float neighbor_sum = 0.0;
    vec2 gradient_sum = vec2(0.0);
    for (int index = 0; index < 8; ++index) {
        float neighbor_height = terrain_source_base_height(parameters,
            world_xz + directions[index] * parameters.weathering.x, footprint_m);
        neighbor_sum += neighbor_height;
        gradient_sum += directions[index] * neighbor_height;
    }
    float neighbor_mean = neighbor_sum / 8.0;
    vec2 gradient = gradient_sum / (4.0 * parameters.weathering.x);
    float slope_activity = smoothstep(0.04, 0.55, length(gradient));
    float footprint_visibility = 1.0 - smoothstep(parameters.weathering.x * 0.25,
        parameters.weathering.x * 0.75, footprint_m);
    float curvature_detail = center_height_m - neighbor_mean;
    return clamp(curvature_detail * 0.45 * slope_activity * footprint_visibility *
        parameters.weathering.z, -parameters.weathering.y, parameters.weathering.y);
}

float terrain_source_height(TerrainSourceGpuParameters parameters, vec2 world_xz,
        float footprint_m) {
    float base_height_m = terrain_source_base_height(parameters, world_xz, footprint_m);
    return base_height_m + terrain_source_weathering_delta(parameters, world_xz, footprint_m,
        base_height_m);
}

TerrainSourceSample terrain_source_sample(TerrainSourceGpuParameters parameters, vec2 world_xz,
        float footprint_m) {
    TerrainSourceSample result;
    result.base_height_m = terrain_source_base_height(parameters, world_xz, footprint_m);
    result.height_m = result.base_height_m + terrain_source_weathering_delta(parameters, world_xz,
        footprint_m, result.base_height_m);
    float step_m = max(parameters.elevation.w, footprint_m * 0.5);
    float x0 = terrain_source_height(parameters, world_xz - vec2(step_m, 0.0), footprint_m);
    float x1 = terrain_source_height(parameters, world_xz + vec2(step_m, 0.0), footprint_m);
    float z0 = terrain_source_height(parameters, world_xz - vec2(0.0, step_m), footprint_m);
    float z1 = terrain_source_height(parameters, world_xz + vec2(0.0, step_m), footprint_m);
    result.gradient_xz = vec2(x1 - x0, z1 - z0) / (2.0 * step_m);
    result.weathering_delta_m = result.height_m - result.base_height_m;
    return result;
}

#endif
