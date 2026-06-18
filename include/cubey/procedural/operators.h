#pragma once

#include <cubey/procedural/field_2d.h>

#include <algorithm>

namespace cubey::procedural {

[[nodiscard]] inline float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] inline float lerp(float a, float b, float t) {
    return a + ((b - a) * t);
}

[[nodiscard]] inline float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] inline float smootherstep01(float value) {
    const float t = saturate(value);
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] ScalarField2D box_blur_3x3(const ScalarField2D& field);
void normalize_to_unit(ScalarField2D& field);

} // namespace cubey::procedural
