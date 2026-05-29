#ifndef CUBEY_OCEAN_MACRO_WAVES_GLSL
#define CUBEY_OCEAN_MACRO_WAVES_GLSL

struct OceanMacroWaveSample {
    vec3 displacement;
    vec2 slope;
    float foam;
};

OceanMacroWaveSample ocean_empty_macro_wave_sample() {
    OceanMacroWaveSample sample_value;
    sample_value.displacement = vec3(0.0);
    sample_value.slope = vec2(0.0);
    sample_value.foam = 0.0;
    return sample_value;
}

vec2 ocean_macro_wave_direction(float wind_angle, float offset) {
    float angle = wind_angle + offset;
    return normalize(vec2(cos(angle), sin(angle)));
}

float ocean_hash21(vec2 value) {
    vec3 p = fract(vec3(value.xyx) * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float ocean_value_noise(vec2 value) {
    vec2 cell = floor(value);
    vec2 local = fract(value);
    vec2 smooth_local = local * local * (3.0 - 2.0 * local);
    float a = ocean_hash21(cell);
    float b = ocean_hash21(cell + vec2(1.0, 0.0));
    float c = ocean_hash21(cell + vec2(0.0, 1.0));
    float d = ocean_hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, smooth_local.x), mix(c, d, smooth_local.x), smooth_local.y);
}

void ocean_add_macro_wave(inout OceanMacroWaveSample sample_value, vec2 position, float time,
                          vec2 direction, float wavelength, float amplitude, float chop,
                          float phase_offset) {
    float k = 6.2831853 / max(wavelength, 0.001);
    float phase_speed = sqrt(9.81 / max(k, 0.0001));
    float phase_warp =
        (ocean_value_noise(position / max(wavelength * 1.65, 1.0) + phase_offset) - 0.5) * 0.60;
    float envelope =
        mix(0.78, 1.14, ocean_value_noise(position / max(wavelength * 2.8, 1.0) +
                                          vec2(phase_offset, -phase_offset)));
    float phase = dot(position, direction) * k + time * phase_speed * k + phase_offset +
                  phase_warp;
    float wave_sin = sin(phase);
    float wave_cos = cos(phase);
    float harmonic_sin = sin(phase * 2.0 + phase_offset);
    float harmonic_cos = cos(phase * 2.0 + phase_offset);
    float local_amplitude = amplitude * envelope;
    float steepness = clamp(chop, 0.0, 1.0);
    float bounded_steepness = min(steepness, 0.82 / max(k * local_amplitude, 0.001));
    float crest_bias = bounded_steepness * bounded_steepness;
    float harmonic_scale = 0.28 * crest_bias;
    float height = (wave_sin + harmonic_sin * harmonic_scale) * local_amplitude;
    float slope = (wave_cos + harmonic_cos * harmonic_scale * 2.0) * local_amplitude * k;

    sample_value.displacement.y += height;
    sample_value.displacement.xz += direction * (wave_cos * local_amplitude * bounded_steepness);
    sample_value.slope += direction * slope;
    float compression = max(wave_sin, 0.0) * local_amplitude * k * bounded_steepness;
    float crest_signal = compression + max(harmonic_sin, 0.0) * harmonic_scale * 1.4;
    sample_value.foam = max(sample_value.foam, smoothstep(0.10, 0.32, crest_signal));
}

void ocean_add_crest_train(inout OceanMacroWaveSample sample_value, vec2 position, float time,
                           vec2 direction, float wavelength, float amplitude, float chop,
                           float phase_offset) {
    float k = 6.2831853 / max(wavelength, 0.001);
    float omega = sqrt(9.81 * k);
    float cross = dot(position, vec2(-direction.y, direction.x));
    float phase_warp =
        (ocean_value_noise(vec2(cross / max(wavelength * 2.4, 1.0), phase_offset)) - 0.5) *
        0.12;
    float envelope =
        mix(0.88, 1.08, ocean_value_noise(position / max(wavelength * 3.1, 1.0) +
                                          vec2(phase_offset * 0.31, phase_offset)));
    float phase = dot(position, direction) * k + time * omega + phase_offset + phase_warp;
    float wave = cos(phase);
    float wave2 = cos(phase * 2.0);
    float wave3 = cos(phase * 3.0);
    float s1 = sin(phase);
    float s2 = sin(phase * 2.0);
    float s3 = sin(phase * 3.0);
    float local_amplitude = amplitude * envelope;
    float steepness = min(max(chop, 0.0), 0.92 / max(k * local_amplitude, 0.001));
    float crest_bias = clamp(steepness * local_amplitude * k, 0.0, 1.0);
    float second_harmonic = 0.42 * crest_bias;
    float third_harmonic = 0.08 * crest_bias * crest_bias;
    float height = (wave + wave2 * second_harmonic + wave3 * third_harmonic) *
                   local_amplitude * 0.58;
    float slope = -(s1 + s2 * second_harmonic * 2.0 + s3 * third_harmonic * 3.0) *
                  local_amplitude * k;

    sample_value.displacement.y += height;
    sample_value.displacement.xz -= direction * (s1 * local_amplitude * steepness * 1.62);
    sample_value.slope += direction * slope;
    float compression = max(0.0, wave) * local_amplitude * k * steepness;
    float crest_line = smoothstep(0.68, 0.96, wave) * smoothstep(0.006, 0.026, crest_bias);
    sample_value.foam =
        max(sample_value.foam, max(smoothstep(0.008, 0.034, compression), crest_line));
}

OceanMacroWaveSample ocean_macro_waves(vec2 position, float time, float wind_angle,
                                       float amplitude, float macro_swell, float chop) {
    OceanMacroWaveSample sample_value = ocean_empty_macro_wave_sample();
    float energy = max(amplitude, 0.0) * clamp(macro_swell, 0.0, 1.5);
    float swell = mix(0.75, 1.65, clamp(macro_swell, 0.0, 1.5) / 1.5);
    ocean_add_macro_wave(sample_value, position, time, ocean_macro_wave_direction(wind_angle, 0.0),
                         640.0 * swell, 1.35 * energy, chop * 0.16, 0.2);
    ocean_add_macro_wave(sample_value, position, time, ocean_macro_wave_direction(wind_angle, 0.52),
                         410.0 * swell, 0.88 * energy, chop * 0.12, 2.4);
    ocean_add_macro_wave(sample_value, position, time, ocean_macro_wave_direction(wind_angle, -0.74),
                         280.0 * swell, 0.48 * energy, chop * 0.08, 5.1);
    ocean_add_macro_wave(sample_value, position, time, ocean_macro_wave_direction(wind_angle, 1.37),
                         210.0 * swell, 0.24 * energy, chop * 0.05, 3.3);
    ocean_add_crest_train(sample_value, position, time,
                          ocean_macro_wave_direction(wind_angle, -0.06), 122.0 * swell,
                          0.42 * energy, chop * 1.00, 4.7);
    ocean_add_crest_train(sample_value, position, time,
                          ocean_macro_wave_direction(wind_angle, 0.04), 86.0 * swell,
                          0.30 * energy, chop * 0.90, 2.8);
    ocean_add_crest_train(sample_value, position, time,
                          ocean_macro_wave_direction(wind_angle, 0.12), 62.0 * swell,
                          0.16 * energy, chop * 0.78, 6.2);
    return sample_value;
}

vec3 ocean_normal_from_slope(vec2 slope) {
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

vec2 ocean_slope_from_normal(vec3 normal) {
    return -normal.xz / max(normal.y, 0.20);
}

#endif
