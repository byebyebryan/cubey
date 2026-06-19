#pragma once

#include <cubey/procedural/field_2d.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace cubey::procedural {

[[nodiscard]] inline float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] inline float lerp(float a, float b, float t) {
    return a + ((b - a) * t);
}

[[nodiscard]] inline float remap(float value, float old_min, float old_max, float new_min,
                                 float new_max) {
    if (old_min == old_max) {
        throw std::runtime_error("procedural remap input range must be non-zero");
    }
    return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
}

[[nodiscard]] inline float smoothstep01(float value) {
    const float t = saturate(value);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] inline float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    return smoothstep01((value - edge0) / (edge1 - edge0));
}

[[nodiscard]] inline float smootherstep01(float value) {
    const float t = saturate(value);
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] float signed_to_unit(float value);
[[nodiscard]] float unit_to_signed(float value);
[[nodiscard]] float pow_unit(float value, float exponent);
[[nodiscard]] float ridge_profile(float value, float sharpness);
[[nodiscard]] float terrace_unit(float value, std::uint32_t steps, float blend);

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

struct ScalarFieldDistribution {
    ScalarFieldStats stats{};
    float p01 = 0.0F;
    float p05 = 0.0F;
    float p10 = 0.0F;
    float p25 = 0.0F;
    float p50 = 0.0F;
    float p75 = 0.0F;
    float p90 = 0.0F;
    float p95 = 0.0F;
    float p99 = 0.0F;
};

[[nodiscard]] ScalarFieldDistribution
summarize_scalar_field_distribution(std::span<const float> values);
[[nodiscard]] ScalarFieldDistribution
summarize_scalar_field_distribution(const ScalarField2D& field);
[[nodiscard]] ScalarField2D box_blur_3x3(const ScalarField2D& field);
[[nodiscard]] ScalarField2D clamp_field(const ScalarField2D& field, float min_value,
                                        float max_value);
[[nodiscard]] ScalarField2D remap_field(const ScalarField2D& field, float in_min, float in_max,
                                        float out_min, float out_max);
[[nodiscard]] ScalarField2D percentile_remap_field(const ScalarField2D& field,
                                                   float low_percentile,
                                                   float high_percentile, float out_min,
                                                   float out_max);
[[nodiscard]] ScalarField2D smoothstep_field(const ScalarField2D& field, float edge0, float edge1);
[[nodiscard]] ScalarField2D invert_unit_field(const ScalarField2D& field);
[[nodiscard]] ScalarField2D signed_to_unit_field(const ScalarField2D& field);
[[nodiscard]] ScalarField2D unit_to_signed_field(const ScalarField2D& field);
[[nodiscard]] ScalarField2D pow_unit_field(const ScalarField2D& field, float exponent);
[[nodiscard]] ScalarField2D ridge_profile_field(const ScalarField2D& field, float sharpness);
[[nodiscard]] ScalarField2D terrace_unit_field(const ScalarField2D& field, std::uint32_t steps,
                                               float blend);
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
