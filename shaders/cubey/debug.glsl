#ifndef CUBEY_DEBUG_GLSL
#define CUBEY_DEBUG_GLSL

#include "cubey/color_space.glsl"

float cubey_debug_saturate_scalar(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 cubey_debug_false_color01(float value) {
    float normalized = cubey_debug_saturate_scalar(value);
    return cubey_hsv_to_linear(vec3((1.0 - normalized) * 0.66, 0.85, 1.0));
}

vec3 cubey_debug_signed_color(float value, float scale) {
    float normalized = cubey_debug_saturate_scalar(abs(value) * max(scale, 0.0));
    vec3 negative_color = vec3(0.15, 0.45, 1.0);
    vec3 positive_color = vec3(1.0, 0.35, 0.1);
    vec3 zero_color = vec3(0.08);
    vec3 lobe = value < 0.0 ? negative_color : positive_color;
    return mix(zero_color, lobe, normalized);
}

vec3 cubey_debug_palette(uint index) {
    switch (index % 8U) {
    case 0U:
        return vec3(0.90, 0.20, 0.18);
    case 1U:
        return vec3(0.15, 0.56, 0.95);
    case 2U:
        return vec3(0.20, 0.75, 0.35);
    case 3U:
        return vec3(0.95, 0.70, 0.18);
    case 4U:
        return vec3(0.60, 0.35, 0.95);
    case 5U:
        return vec3(0.10, 0.80, 0.80);
    case 6U:
        return vec3(0.95, 0.45, 0.75);
    default:
        return vec3(0.85);
    }
}

float cubey_debug_checker(vec2 uv, float cells_per_unit) {
    vec2 cell = floor(uv * max(cells_per_unit, 0.0001));
    return mod(cell.x + cell.y, 2.0);
}

#endif
