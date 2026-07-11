#ifndef CUBEY_TERRAIN_SOURCE_GLSL
#define CUBEY_TERRAIN_SOURCE_GLSL

#include "cubey/procedural/fastnoise_lite.glsl"

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
};

struct TerrainSourceSample {
    float base_height_m;
    float height_m;
    vec2 gradient_xz;
    float weathering_delta_m;
};

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
        if (footprint_weight > 0.0) {
            noise.seed = band.control.x + octave * 1013;
            float octave_f = float(octave);
            float sample_value = fnlGetNoise2D(noise,
                rotated.x * frequency + octave_f * 17.31,
                rotated.y * frequency - octave_f * 9.17);
            float unit_value = sample_value * 0.5 + 0.5;
            float ridge = 1.0 - abs(sample_value);
            float shaped = mix(unit_value, ridge, band.shape.w);
            float octave_weight = amplitude * footprint_weight;
            value += shaped * octave_weight;
            weight += octave_weight;
        }
        frequency *= band.shape.y;
        amplitude *= band.shape.z;
    }
    return weight > 0.0 ? value / weight : 0.5;
}

float terrain_source_base_height(TerrainSourceGpuParameters parameters, vec2 world_xz,
        float footprint_m) {
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
    if (parameters.weathering.w <= 0.5 || parameters.weathering.z <= 0.0) {
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
