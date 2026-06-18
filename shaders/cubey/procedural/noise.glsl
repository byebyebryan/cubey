#ifndef CUBEY_PROCEDURAL_NOISE_GLSL
#define CUBEY_PROCEDURAL_NOISE_GLSL

#include "cubey/procedural/operators.glsl"
#include "cubey/procedural/random.glsl"

float cubey_proc_hash_sindot_2d(vec2 value) {
    return fract(sin(dot(value, vec2(127.1, 311.7))) * 43758.5453);
}

float cubey_proc_value_noise_sindot_2d(vec2 value) {
    vec2 cell = floor(value);
    vec2 local = fract(value);
    vec2 shaped = cubey_proc_smoothstep01(local);
    float a = cubey_proc_hash_sindot_2d(cell);
    float b = cubey_proc_hash_sindot_2d(cell + vec2(1.0, 0.0));
    float c = cubey_proc_hash_sindot_2d(cell + vec2(0.0, 1.0));
    float d = cubey_proc_hash_sindot_2d(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, shaped.x), mix(c, d, shaped.x), shaped.y);
}

float cubey_proc_value_noise_pcg_2d(vec2 value) {
    vec2 cell = floor(value);
    vec2 local = fract(value);
    vec2 shaped = cubey_proc_smoothstep01(local);
    float a = cubey_proc_hash_pcg_2d(cell);
    float b = cubey_proc_hash_pcg_2d(cell + vec2(1.0, 0.0));
    float c = cubey_proc_hash_pcg_2d(cell + vec2(0.0, 1.0));
    float d = cubey_proc_hash_pcg_2d(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, shaped.x), mix(c, d, shaped.x), shaped.y);
}

uint cubey_proc_hash_u32_3d(ivec3 p, uint seed) {
    uint value = seed;
    value ^= uint(p.x) * 0x9e3779b9U;
    value ^= uint(p.y) * 0x85ebca6bU;
    value ^= uint(p.z) * 0xc2b2ae35U;
    return cubey_proc_hash_u32(value);
}

float cubey_proc_hash01_3d(ivec3 p, uint seed) {
    return float(cubey_proc_hash_u32_3d(p, seed) >> 8U) / 16777215.0;
}

float cubey_proc_value_noise_3d(vec3 value, uint seed) {
    ivec3 cell = ivec3(floor(value));
    vec3 shaped = cubey_proc_smootherstep01(value - vec3(cell));

    float x00 = mix(cubey_proc_hash01_3d(cell + ivec3(0, 0, 0), seed) * 2.0 - 1.0,
                    cubey_proc_hash01_3d(cell + ivec3(1, 0, 0), seed) * 2.0 - 1.0,
                    shaped.x);
    float x10 = mix(cubey_proc_hash01_3d(cell + ivec3(0, 1, 0), seed) * 2.0 - 1.0,
                    cubey_proc_hash01_3d(cell + ivec3(1, 1, 0), seed) * 2.0 - 1.0,
                    shaped.x);
    float x01 = mix(cubey_proc_hash01_3d(cell + ivec3(0, 0, 1), seed) * 2.0 - 1.0,
                    cubey_proc_hash01_3d(cell + ivec3(1, 0, 1), seed) * 2.0 - 1.0,
                    shaped.x);
    float x11 = mix(cubey_proc_hash01_3d(cell + ivec3(0, 1, 1), seed) * 2.0 - 1.0,
                    cubey_proc_hash01_3d(cell + ivec3(1, 1, 1), seed) * 2.0 - 1.0,
                    shaped.x);
    return mix(mix(x00, x10, shaped.y), mix(x01, x11, shaped.y), shaped.z);
}

float cubey_proc_fbm_3d(vec3 value, uint seed, uint octaves) {
    float amplitude = 0.5;
    float frequency = 1.0;
    float sum = 0.0;
    float weight = 0.0;
    for (uint octave = 0U; octave < octaves; ++octave) {
        sum += cubey_proc_value_noise_3d(value * frequency, seed + octave * 1013U) * amplitude;
        weight += amplitude;
        frequency *= 2.03;
        amplitude *= 0.5;
    }
    return weight > 0.0 ? sum / weight : 0.0;
}

#endif
