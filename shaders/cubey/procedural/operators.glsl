#ifndef CUBEY_PROCEDURAL_OPERATORS_GLSL
#define CUBEY_PROCEDURAL_OPERATORS_GLSL

// Exact CPU parity contract: cubey::procedural scalar helpers mirror these
// formulas for valid input ranges.
float cubey_proc_saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec2 cubey_proc_saturate(vec2 value) {
    return clamp(value, vec2(0.0), vec2(1.0));
}

vec3 cubey_proc_saturate(vec3 value) {
    return clamp(value, vec3(0.0), vec3(1.0));
}

float cubey_proc_remap(float value, float old_min, float old_max, float new_min, float new_max) {
    return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
}

float cubey_proc_smoothstep01(float value) {
    float x = cubey_proc_saturate(value);
    return x * x * (3.0 - (2.0 * x));
}

vec2 cubey_proc_smoothstep01(vec2 value) {
    vec2 x = cubey_proc_saturate(value);
    return x * x * (vec2(3.0) - (vec2(2.0) * x));
}

vec3 cubey_proc_smoothstep01(vec3 value) {
    vec3 x = cubey_proc_saturate(value);
    return x * x * (vec3(3.0) - (vec3(2.0) * x));
}

float cubey_proc_smootherstep01(float value) {
    float x = cubey_proc_saturate(value);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

vec3 cubey_proc_smootherstep01(vec3 value) {
    vec3 x = cubey_proc_saturate(value);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

#endif
