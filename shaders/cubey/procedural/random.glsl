#ifndef CUBEY_PROCEDURAL_RANDOM_GLSL
#define CUBEY_PROCEDURAL_RANDOM_GLSL

// Exact CPU parity contract: cubey::procedural::hash_u32 mirrors this formula.
uint cubey_proc_hash_u32(uint value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

// Shader masked hash-to-unit contract. This intentionally differs from the
// legacy CPU high-bit hash_to_unit helper; use hash_to_unit_masked_24 for CPU
// parity with this GLSL helper.
float cubey_proc_hash01_u32(uint value) {
    return float(cubey_proc_hash_u32(value) & 0x00ffffffU) / float(0x01000000U);
}

// Shared shader visual formulas. These are not CPU procedural contracts yet.
float cubey_proc_hash_pcg_2d(vec2 value) {
    vec3 p3 = fract(vec3(value.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float cubey_proc_hash_pcg_3d(vec3 value) {
    vec3 p3 = fract(value * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

#endif
