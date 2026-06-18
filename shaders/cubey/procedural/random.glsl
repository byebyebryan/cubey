#ifndef CUBEY_PROCEDURAL_RANDOM_GLSL
#define CUBEY_PROCEDURAL_RANDOM_GLSL

uint cubey_proc_hash_u32(uint value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

float cubey_proc_hash01_u32(uint value) {
    return float(cubey_proc_hash_u32(value) & 0x00ffffffU) / float(0x01000000U);
}

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
