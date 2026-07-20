float terrain_engine_reference_random2d(vec2 p, vec2 seed) {
    float dot_value = (p.x * (12.9898 + seed.x)) + (p.y * (78.233 + seed.y));
    return fract(sin(dot_value) * 43758.5453123);
}

float terrain_engine_reference_noise(vec2 p, vec2 seed) {
    vec2 integer_part = floor(p);
    vec2 fractional_part = p - integer_part;
    float a = terrain_engine_reference_random2d(integer_part, seed);
    float b = terrain_engine_reference_random2d(integer_part + vec2(1.0, 0.0), seed);
    float c = terrain_engine_reference_random2d(integer_part + vec2(0.0, 1.0), seed);
    float d = terrain_engine_reference_random2d(integer_part + vec2(1.0, 1.0), seed);
    vec2 w = fractional_part * fractional_part * fractional_part *
        (vec2(10.0) + fractional_part * (vec2(-15.0) + 6.0 * fractional_part));
    float k0 = a;
    float k1 = b - a;
    float k2 = c - a;
    float k3 = d - c - b + a;
    return k0 + (k1 * w.x) + (k2 * w.y) + (k3 * w.x * w.y);
}

float terrain_engine_reference_height(vec2 world, vec2 seed) {
    const int octaves = 13;
    const float input_frequency = 0.01;
    const float displacement_factor = 20.0;
    const float persistence = 0.5;
    const float power = 3.0;
    float frequency = 0.005 * input_frequency;
    float amplitude = displacement_factor;
    float total = 0.0;
    mat2 rotation = mat2(0.8, -0.6, 0.6, 0.8);
    for (int octave = 0; octave < octaves; ++octave) {
        frequency *= 2.0;
        amplitude *= persistence;
        total += terrain_engine_reference_noise(frequency * (rotation * world), seed) * amplitude;
    }
    return pow(max(total, 0.0), power);
}

vec3 terrain_engine_reference_normal(vec2 world, vec2 seed, float vertical_scale) {
    const float step_m = 1.0;
    float dhdx = (terrain_engine_reference_height(world + vec2(step_m, 0.0), seed) -
                  terrain_engine_reference_height(world - vec2(step_m, 0.0), seed)) *
        vertical_scale / (2.0 * step_m);
    float dhdz = (terrain_engine_reference_height(world + vec2(0.0, step_m), seed) -
                  terrain_engine_reference_height(world - vec2(0.0, step_m), seed)) *
        vertical_scale / (2.0 * step_m);
    return normalize(vec3(-dhdx, 1.0, -dhdz));
}

vec3 terrain_engine_reference_material_color(float height_m, float cos_v, float water_mask) {
    const float grass_coverage = 0.65;
    const float transition_m = 20.0;
    const float water_height_m = 100.0;
    const float ten_percent_grass = grass_coverage - (grass_coverage * 0.1);
    vec3 rock = vec3(0.46, 0.45, 0.39);
    vec3 soil = vec3(0.39, 0.31, 0.20);
    vec3 grass = vec3(0.23, 0.35, 0.17);
    vec3 color = rock;
    if (height_m <= water_height_m + transition_m) {
        color = soil;
    } else if (height_m <= water_height_m + (2.0 * transition_m)) {
        float blend = clamp((height_m - water_height_m - transition_m) / transition_m, 0.0, 1.0);
        color = mix(soil, grass, blend);
    } else if (cos_v > grass_coverage) {
        color = grass;
    } else if (cos_v > ten_percent_grass) {
        float blend = clamp((cos_v - ten_percent_grass) / (grass_coverage * 0.1), 0.0, 1.0);
        color = mix(rock, grass, blend);
    }
    return mix(color, vec3(0.05, 0.24, 0.34), water_mask);
}
