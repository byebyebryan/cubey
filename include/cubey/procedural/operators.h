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

[[nodiscard]] float ridge_profile(float value, float sharpness);

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
[[nodiscard]] ScalarField2D clamp_field(const ScalarField2D& field, float min_value,
                                        float max_value);
[[nodiscard]] ScalarField2D remap_field(const ScalarField2D& field, float in_min, float in_max,
                                        float out_min, float out_max);
[[nodiscard]] ScalarField2D smoothstep_field(const ScalarField2D& field, float edge0, float edge1);
[[nodiscard]] ScalarField2D invert_unit_field(const ScalarField2D& field);
[[nodiscard]] ScalarField2D ridge_profile_field(const ScalarField2D& field, float sharpness);
[[nodiscard]] ScalarField2D add_fields(const ScalarField2D& lhs, const ScalarField2D& rhs);
[[nodiscard]] ScalarField2D subtract_fields(const ScalarField2D& lhs, const ScalarField2D& rhs);
[[nodiscard]] ScalarField2D multiply_fields(const ScalarField2D& lhs, const ScalarField2D& rhs);
[[nodiscard]] ScalarField2D min_fields(const ScalarField2D& lhs, const ScalarField2D& rhs);
[[nodiscard]] ScalarField2D max_fields(const ScalarField2D& lhs, const ScalarField2D& rhs);
[[nodiscard]] ScalarField2D blend_fields(const ScalarField2D& lhs, const ScalarField2D& rhs,
                                         const ScalarField2D& mask);
[[nodiscard]] SlopeCurvature2D compute_slope_curvature(const ScalarField2D& field);
[[nodiscard]] LocalRelief2D compute_local_relief(const ScalarField2D& field,
                                                 std::uint32_t radius_samples);
void normalize_to_unit(ScalarField2D& field);

} // namespace cubey::procedural
