#pragma once

#include <cubey/procedural/field_2d.h>

#include <algorithm>
#include <cstdint>

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

struct SlopeCurvature2D {
    ScalarField2D slope{};
    ScalarField2D curvature{};
    float max_slope = 0.0F;
    float max_abs_curvature = 0.0F;
};

struct LocalRelief2D {
    ScalarField2D local_min{};
    ScalarField2D local_max{};
    ScalarField2D local_mean{};
    ScalarField2D local_span{};
};

[[nodiscard]] ScalarField2D box_blur_3x3(const ScalarField2D& field);
[[nodiscard]] SlopeCurvature2D compute_slope_curvature(const ScalarField2D& field);
[[nodiscard]] LocalRelief2D compute_local_relief(const ScalarField2D& field,
                                                 std::uint32_t radius_samples);
void normalize_to_unit(ScalarField2D& field);

} // namespace cubey::procedural
