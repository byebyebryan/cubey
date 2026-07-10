const float SHADERTOY_EROSION_PI = 3.14159265358979323846;
const float SHADERTOY_EROSION_TAU = 2.0 * SHADERTOY_EROSION_PI;
const float SHADERTOY_EROSION_BASE_FREQUENCY = 1.0 / 14000.0;
const float SHADERTOY_EROSION_BASE_HEIGHT_M = 180.0;
const float SHADERTOY_EROSION_BASE_RELIEF_M = 3200.0;
const float SHADERTOY_EROSION_SCALE_M = 1800.0;
const float SHADERTOY_EROSION_STRENGTH_M = 240.0;
const float SHADERTOY_EROSION_GAIN = 0.5;
const float SHADERTOY_EROSION_LACUNARITY = 2.0;
const int SHADERTOY_EROSION_OCTAVES = 5;

struct ShadertoyErosionHeightSlope {
    float height;
    vec2 gradient;
};

struct ShadertoyErosionDirectionalSample {
    float value;
    vec2 gradient;
};

struct ShadertoyErosionReferenceSample {
    float base_height_m;
    float filtered_height_m;
    float erosion_delta_m;
    vec2 filtered_gradient;
};

vec2 shadertoy_erosion_rotate(vec2 value, float angle) {
    float cosine = cos(angle);
    float sine = sin(angle);
    return vec2((cosine * value.x) - (sine * value.y),
        (sine * value.x) + (cosine * value.y));
}

float shadertoy_erosion_smootherstep01(float value) {
    return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

float shadertoy_erosion_smootherstep_derivative(float value) {
    return 30.0 * value * value * (value * (value - 2.0) + 1.0);
}

float shadertoy_erosion_hash01(vec2 p, vec2 seed, float salt) {
    float value = ((p.x + seed.x + salt) * 127.1) +
        ((p.y + seed.y - salt * 0.37) * 311.7);
    return fract(sin(value) * 43758.5453);
}

vec2 shadertoy_erosion_gradient_hash(vec2 p, vec2 seed) {
    float angle = shadertoy_erosion_hash01(p, seed, 0.0) * SHADERTOY_EROSION_TAU;
    return vec2(cos(angle), sin(angle));
}

ShadertoyErosionHeightSlope shadertoy_erosion_gradient_noise(vec2 p, vec2 seed) {
    vec2 integer_part = floor(p);
    vec2 fractional = p - integer_part;
    vec2 ga = shadertoy_erosion_gradient_hash(integer_part, seed);
    vec2 gb = shadertoy_erosion_gradient_hash(integer_part + vec2(1.0, 0.0), seed);
    vec2 gc = shadertoy_erosion_gradient_hash(integer_part + vec2(0.0, 1.0), seed);
    vec2 gd = shadertoy_erosion_gradient_hash(integer_part + vec2(1.0, 1.0), seed);
    float va = dot(ga, fractional);
    float vb = dot(gb, fractional - vec2(1.0, 0.0));
    float vc = dot(gc, fractional - vec2(0.0, 1.0));
    float vd = dot(gd, fractional - vec2(1.0, 1.0));
    float ux = shadertoy_erosion_smootherstep01(fractional.x);
    float uy = shadertoy_erosion_smootherstep01(fractional.y);
    float dux = shadertoy_erosion_smootherstep_derivative(fractional.x);
    float duy = shadertoy_erosion_smootherstep_derivative(fractional.y);
    float x0 = mix(va, vb, ux);
    float x1 = mix(vc, vd, ux);
    float dx0 = mix(ga.x, gb.x, ux) + (vb - va) * dux;
    float dx1 = mix(gc.x, gd.x, ux) + (vd - vc) * dux;
    float dy0 = mix(ga.y, gb.y, ux);
    float dy1 = mix(gc.y, gd.y, ux);
    return ShadertoyErosionHeightSlope(
        mix(x0, x1, uy),
        vec2(mix(dx0, dx1, uy), mix(dy0, dy1, uy) + (x1 - x0) * duy));
}

ShadertoyErosionHeightSlope shadertoy_erosion_base_mountain(vec2 world, vec2 seed) {
    const float amplitudes[4] = float[4](1.0, 0.42, 0.18, 0.075);
    float raw = 0.0;
    vec2 raw_gradient = vec2(0.0);
    float amplitude_sum = 0.0;
    float frequency = SHADERTOY_EROSION_BASE_FREQUENCY;
    for (int octave = 0; octave < 4; ++octave) {
        float angle = float(octave) * 0.61;
        vec2 domain = shadertoy_erosion_rotate(world * frequency, angle);
        ShadertoyErosionHeightSlope noise = shadertoy_erosion_gradient_noise(domain,
            seed + vec2(float(octave) * 7.13, -float(octave) * 5.71));
        float amplitude = amplitudes[octave];
        raw += noise.height * amplitude;
        raw_gradient += shadertoy_erosion_rotate(noise.gradient, -angle) *
            (frequency * amplitude);
        amplitude_sum += amplitude;
        frequency *= 2.03;
    }

    float normalized = raw / amplitude_sum;
    vec2 normalized_gradient = raw_gradient / amplitude_sum;
    float unit_value = clamp((normalized * 0.82) + 0.52, 0.0, 1.0);
    float profile_t = clamp((unit_value - 0.24) / 0.56, 0.0, 1.0);
    float profile = profile_t * profile_t * (3.0 - (2.0 * profile_t));
    float profile_derivative = profile_t > 0.0 && profile_t < 1.0
        ? (6.0 * profile_t * (1.0 - profile_t) / 0.56) * 0.82
        : 0.0;
    return ShadertoyErosionHeightSlope(
        SHADERTOY_EROSION_BASE_HEIGHT_M + (profile * SHADERTOY_EROSION_BASE_RELIEF_M) +
            (normalized * 180.0),
        normalized_gradient * ((profile_derivative * SHADERTOY_EROSION_BASE_RELIEF_M) + 180.0));
}

ShadertoyErosionDirectionalSample shadertoy_erosion_directional_cells(
    vec2 p, vec2 slope, vec2 seed) {
    float slope_length = length(slope);
    if (slope_length <= 1.0e-5) {
        return ShadertoyErosionDirectionalSample(1.0, vec2(0.0));
    }
    vec2 downhill = slope * (-1.0 / slope_length);
    float shaped_slope = pow(slope_length, 0.68);
    vec2 side_direction = vec2(-downhill.y, downhill.x) *
        (shaped_slope * SHADERTOY_EROSION_TAU);
    vec2 integer_part = floor(p);
    vec2 fractional = p - integer_part;
    float value_sum = 0.0;
    vec2 gradient_sum = vec2(0.0);
    float weight_sum = 0.0;
    vec2 weight_gradient_sum = vec2(0.0);

    for (int y = -1; y <= 2; ++y) {
        for (int x = -1; x <= 2; ++x) {
            vec2 cell = integer_part + vec2(float(x), float(y));
            vec2 jitter = vec2(
                shadertoy_erosion_hash01(cell, seed, 17.0) - 0.5,
                shadertoy_erosion_hash01(cell, seed, 43.0) - 0.5) * 0.82;
            vec2 relative = fractional - vec2(float(x), float(y)) - jitter;
            float distance_squared = dot(relative, relative);
            float exponential_value = exp(-2.0 * distance_squared);
            float weight = max(0.0, exponential_value - 0.0111);
            if (weight <= 0.0) {
                continue;
            }
            vec2 weight_gradient = relative * (-4.0 * exponential_value);
            float phase = dot(relative, side_direction) +
                (shadertoy_erosion_hash01(cell, seed, 79.0) - 0.5) * 0.70;
            float value = cos(phase);
            vec2 value_gradient = side_direction * -sin(phase);
            value_sum += value * weight;
            gradient_sum += value_gradient * weight + weight_gradient * value;
            weight_sum += weight;
            weight_gradient_sum += weight_gradient;
        }
    }

    if (weight_sum <= 1.0e-6) {
        return ShadertoyErosionDirectionalSample(0.0, vec2(0.0));
    }
    float value = value_sum / weight_sum;
    vec2 gradient = (gradient_sum * weight_sum - weight_gradient_sum * value_sum) /
        (weight_sum * weight_sum);
    return ShadertoyErosionDirectionalSample(value, gradient);
}

ShadertoyErosionHeightSlope shadertoy_erosion_filtered_mountain(
    vec2 world, vec2 seed, ShadertoyErosionHeightSlope base) {
    ShadertoyErosionHeightSlope filtered = base;
    float scale_m = SHADERTOY_EROSION_SCALE_M;
    float strength_m = SHADERTOY_EROSION_STRENGTH_M;
    for (int octave = 0; octave < SHADERTOY_EROSION_OCTAVES; ++octave) {
        float slope_gate = smoothstep(0.025, 0.32, length(filtered.gradient));
        float mountain_gate = smoothstep(420.0, 1180.0, base.height);
        float gate = slope_gate * mountain_gate;
        vec2 octave_seed = seed + vec2(101.0 + float(octave) * 37.0,
            -131.0 - float(octave) * 29.0);
        ShadertoyErosionDirectionalSample gully = shadertoy_erosion_directional_cells(
            world / scale_m, filtered.gradient, octave_seed);
        float height_delta = (gully.value - 0.28) * strength_m * gate;
        filtered.height += height_delta;
        filtered.gradient += gully.gradient * ((strength_m / scale_m) * gate);
        strength_m *= SHADERTOY_EROSION_GAIN;
        scale_m /= SHADERTOY_EROSION_LACUNARITY;
    }
    return filtered;
}

ShadertoyErosionReferenceSample shadertoy_erosion_reference_sample(vec2 world, vec2 seed) {
    ShadertoyErosionHeightSlope base = shadertoy_erosion_base_mountain(world, seed);
    ShadertoyErosionHeightSlope filtered = shadertoy_erosion_filtered_mountain(world, seed, base);
    return ShadertoyErosionReferenceSample(base.height, filtered.height,
        base.height - filtered.height, filtered.gradient);
}

float shadertoy_erosion_reference_height(vec2 world, vec2 seed, bool filtered_surface) {
    ShadertoyErosionReferenceSample sample = shadertoy_erosion_reference_sample(world, seed);
    return filtered_surface ? sample.filtered_height_m : sample.base_height_m;
}

vec3 shadertoy_erosion_reference_normal(vec2 world, vec2 seed, float vertical_scale) {
    ShadertoyErosionReferenceSample sample = shadertoy_erosion_reference_sample(world, seed);
    return normalize(vec3(-sample.filtered_gradient.x * vertical_scale, 1.0,
        -sample.filtered_gradient.y * vertical_scale));
}

