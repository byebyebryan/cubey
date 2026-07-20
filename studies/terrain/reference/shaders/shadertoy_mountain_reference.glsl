struct ShadertoyMountainNoiseSample {
    float value;
    vec2 gradient;
};

float shadertoy_mountain_fract_positive(float value) {
    return value - floor(value);
}

float shadertoy_mountain_smootherstep(float value) {
    return value * value * value * (10.0 + value * (-15.0 + 6.0 * value));
}

vec2 shadertoy_mountain_rotate(vec2 value) {
    return vec2((0.82 * value.x) - (0.57 * value.y),
        (0.57 * value.x) + (0.82 * value.y));
}

float shadertoy_mountain_seeded_hash(vec2 p, vec2 seed) {
    float dot_value = ((p.x + seed.x) * 97.1371) + ((p.y + seed.y) * 213.357) + 19.19;
    return shadertoy_mountain_fract_positive(sin(dot_value) * 15347.9162);
}

float shadertoy_mountain_value_noise(vec2 p, vec2 seed) {
    vec2 integer_part = floor(p);
    vec2 fractional = p - integer_part;
    float a = shadertoy_mountain_seeded_hash(integer_part, seed);
    float b = shadertoy_mountain_seeded_hash(integer_part + vec2(1.0, 0.0), seed);
    float c = shadertoy_mountain_seeded_hash(integer_part + vec2(0.0, 1.0), seed);
    float d = shadertoy_mountain_seeded_hash(integer_part + vec2(1.0, 1.0), seed);
    vec2 w = vec2(shadertoy_mountain_smootherstep(fractional.x),
        shadertoy_mountain_smootherstep(fractional.y));
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}

ShadertoyMountainNoiseSample shadertoy_mountain_noise(vec2 p, vec2 seed) {
    const float gradient_step = 0.35;
    float center = shadertoy_mountain_value_noise(p, seed);
    float dx = shadertoy_mountain_value_noise(p + vec2(gradient_step, 0.0), seed) -
        shadertoy_mountain_value_noise(p - vec2(gradient_step, 0.0), seed);
    float dy = shadertoy_mountain_value_noise(p + vec2(0.0, gradient_step), seed) -
        shadertoy_mountain_value_noise(p - vec2(0.0, gradient_step), seed);
    return ShadertoyMountainNoiseSample(center, vec2(dx, dy) / (2.0 * gradient_step));
}

float shadertoy_mountain_macro_fbm(vec2 p, vec2 seed) {
    float value = 0.0;
    float amplitude = 0.55;
    float total_amplitude = 0.0;
    for (int octave = 0; octave < 5; ++octave) {
        value += shadertoy_mountain_value_noise(p,
            seed + vec2(float(octave) * 5.31, -float(octave) * 3.73)) * amplitude;
        total_amplitude += amplitude;
        p = shadertoy_mountain_rotate((p * 1.93) + vec2(11.0, -7.0));
        amplitude *= 0.52;
    }
    return total_amplitude > 0.0 ? value / total_amplitude : 0.0;
}

float shadertoy_mountain_reference_height(vec2 world, vec2 seed, bool surface_detail) {
    vec2 p = (world * 0.00026) + (seed * vec2(0.137, 0.113)) + vec2(7.0, -5.0);
    float macro = shadertoy_mountain_macro_fbm((p * 0.42), seed + vec2(2.0, -3.0));
    float support = smoothstep(0.08, 0.68, macro);
    float range = max(support, 0.0);

    vec2 warp = vec2(0.0);
    float frequency = 1.0;
    float amplitude = 1.0;
    float total = 0.0;
    float total_amplitude = 0.0;
    const int max_octaves = 8;
    int octave_count = surface_detail ? 8 : 5;
    for (int octave = 0; octave < max_octaves; ++octave) {
        if (octave >= octave_count) {
            break;
        }
        float octave_weight = octave < 3 ? 1.0 : (surface_detail ? 0.42 : 0.68);
        vec2 sample_p = (p * frequency) + warp + vec2(float(octave) * 17.31,
            -float(octave) * 9.17);
        ShadertoyMountainNoiseSample noise_sample = shadertoy_mountain_noise(sample_p,
            seed + vec2(float(octave) * 1.91, -float(octave) * 2.47));
        float ridge = pow(max(1.0 - abs((noise_sample.value * 2.0) - 1.0), 0.0), 1.75);
        float billow = pow(max(noise_sample.value, 0.0), 2.35);
        float shaped = mix(billow, ridge, 0.46);
        total += shaped * amplitude * octave_weight * (0.64 + 0.36 * support);
        total_amplitude += amplitude * octave_weight;
        warp += noise_sample.gradient * amplitude * octave_weight * (0.66 + 0.04 * float(octave));
        frequency *= 1.92;
        amplitude *= 0.48 + 0.035 * noise_sample.value;
    }

    float detail = total_amplitude > 0.0 ? total / total_amplitude : 0.0;
    float broad_base = pow(max(macro, 0.0), 2.20) * 900.0;
    float mountain = pow(max(detail, 0.0), 1.08) * range * 3100.0;
    float valley_cut = pow(max(1.0 - macro, 0.0), 2.0) * 180.0;
    return max(broad_base + mountain - valley_cut, 0.0);
}

vec3 shadertoy_mountain_reference_normal(vec2 world, vec2 seed, float vertical_scale) {
    const float step_m = 1.0;
    float dhdx = (shadertoy_mountain_reference_height(world + vec2(step_m, 0.0), seed, true) -
                  shadertoy_mountain_reference_height(world - vec2(step_m, 0.0), seed, true)) *
        vertical_scale / (2.0 * step_m);
    float dhdz = (shadertoy_mountain_reference_height(world + vec2(0.0, step_m), seed, true) -
                  shadertoy_mountain_reference_height(world - vec2(0.0, step_m), seed, true)) *
        vertical_scale / (2.0 * step_m);
    return normalize(vec3(-dhdx, 1.0, -dhdz));
}
