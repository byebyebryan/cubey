#include <cubey/procedural/operators.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace cubey::procedural {
namespace {

[[nodiscard]] bool same_desc(const Grid2DDesc& lhs, const Grid2DDesc& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.cell_size == rhs.cell_size &&
           lhs.origin_x == rhs.origin_x && lhs.origin_y == rhs.origin_y;
}

void require_same_desc(const ScalarField2D& lhs, const ScalarField2D& rhs) {
    if (!same_desc(lhs.desc(), rhs.desc())) {
        throw std::runtime_error("procedural scalar field descriptors must match");
    }
}

template <typename Fn>
[[nodiscard]] ScalarField2D transform_field(const ScalarField2D& field, Fn&& fn) {
    ScalarField2D result(field.desc());
    const std::span<const float> source = field.values();
    std::span<float> target = result.values();
    for (std::size_t index = 0; index < source.size(); ++index) {
        target[index] = fn(source[index]);
    }
    return result;
}

template <typename Fn>
[[nodiscard]] ScalarField2D combine_fields(const ScalarField2D& lhs, const ScalarField2D& rhs,
                                           Fn&& fn) {
    require_same_desc(lhs, rhs);
    ScalarField2D result(lhs.desc());
    const std::span<const float> lhs_values = lhs.values();
    const std::span<const float> rhs_values = rhs.values();
    std::span<float> target = result.values();
    for (std::size_t index = 0; index < lhs_values.size(); ++index) {
        target[index] = fn(lhs_values[index], rhs_values[index]);
    }
    return result;
}

[[nodiscard]] float percentile_from_sorted(std::span<const float> sorted_values, float percentile) {
    if (sorted_values.empty()) {
        return 0.0F;
    }
    const float clamped = saturate(percentile);
    const float position = clamped * static_cast<float>(sorted_values.size() - 1U);
    const auto lower_index = static_cast<std::size_t>(std::floor(position));
    const auto upper_index = static_cast<std::size_t>(std::ceil(position));
    const float fraction = position - static_cast<float>(lower_index);
    return lerp(sorted_values[lower_index], sorted_values[upper_index], fraction);
}

void require_percentile_range(float low_percentile, float high_percentile) {
    if (!std::isfinite(low_percentile) || !std::isfinite(high_percentile) ||
        low_percentile < 0.0F || high_percentile > 1.0F || low_percentile >= high_percentile) {
        throw std::runtime_error("procedural percentile range must be finite and increasing");
    }
}

} // namespace

float signed_to_unit(float value) {
    return saturate((value * 0.5F) + 0.5F);
}

float unit_to_signed(float value) {
    return (saturate(value) * 2.0F) - 1.0F;
}

float pow_unit(float value, float exponent) {
    if (!std::isfinite(exponent) || exponent <= 0.0F) {
        throw std::runtime_error("procedural pow_unit exponent must be positive");
    }
    return std::pow(saturate(value), exponent);
}

float ridge_profile(float value, float sharpness) {
    return std::pow(std::max(1.0F - std::abs(value), 0.0F), sharpness);
}

float terrace_unit(float value, std::uint32_t steps, float blend) {
    if (steps < 2U) {
        throw std::runtime_error("procedural terrace steps must be at least 2");
    }
    const float t = saturate(value);
    if (t >= 1.0F) {
        return 1.0F;
    }
    const float interval_count = static_cast<float>(steps - 1U);
    const float scaled = t * interval_count;
    const float lower_index = std::floor(scaled);
    const float fraction = scaled - lower_index;
    const float lower = lower_index / interval_count;
    const float upper = (lower_index + 1.0F) / interval_count;
    const float transition = saturate(blend);
    if (transition == 0.0F) {
        return lower;
    }
    return lerp(lower, upper, smoothstep(1.0F - transition, 1.0F, fraction));
}

ScalarField2D box_blur_3x3(const ScalarField2D& field) {
    ScalarField2D result(field.desc());
    const Grid2DDesc& desc = field.desc();
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            float sum = 0.0F;
            float weight_sum = 0.0F;
            for (std::int32_t oy = -1; oy <= 1; ++oy) {
                const auto sy = static_cast<std::int32_t>(y) + oy;
                if (sy < 0 || sy >= static_cast<std::int32_t>(desc.height)) {
                    continue;
                }
                for (std::int32_t ox = -1; ox <= 1; ++ox) {
                    const auto sx = static_cast<std::int32_t>(x) + ox;
                    if (sx < 0 || sx >= static_cast<std::int32_t>(desc.width)) {
                        continue;
                    }
                    const float weight = ox == 0 && oy == 0   ? 4.0F
                                         : ox == 0 || oy == 0 ? 2.0F
                                                              : 1.0F;
                    sum +=
                        field.at(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy)) *
                        weight;
                    weight_sum += weight;
                }
            }
            result.at(x, y) = weight_sum == 0.0F ? field.at(x, y) : sum / weight_sum;
        }
    }
    return result;
}

ScalarField2D clamp_field(const ScalarField2D& field, float min_value, float max_value) {
    return transform_field(field, [min_value, max_value](float value) {
        return std::clamp(value, min_value, max_value);
    });
}

ScalarField2D remap_field(const ScalarField2D& field, float in_min, float in_max, float out_min,
                          float out_max) {
    if (in_min == in_max) {
        throw std::runtime_error("procedural remap input range must be non-zero");
    }
    return transform_field(field, [in_min, in_max, out_min, out_max](float value) {
        const float t = saturate((value - in_min) / (in_max - in_min));
        return lerp(out_min, out_max, t);
    });
}

ScalarField2D smoothstep_field(const ScalarField2D& field, float edge0, float edge1) {
    return transform_field(field,
                           [edge0, edge1](float value) { return smoothstep(edge0, edge1, value); });
}

ScalarField2D invert_unit_field(const ScalarField2D& field) {
    return transform_field(field, [](float value) { return 1.0F - saturate(value); });
}

ScalarField2D signed_to_unit_field(const ScalarField2D& field) {
    return transform_field(field, [](float value) { return signed_to_unit(value); });
}

ScalarField2D unit_to_signed_field(const ScalarField2D& field) {
    return transform_field(field, [](float value) { return unit_to_signed(value); });
}

ScalarField2D pow_unit_field(const ScalarField2D& field, float exponent) {
    return transform_field(field, [exponent](float value) { return pow_unit(value, exponent); });
}

ScalarField2D ridge_profile_field(const ScalarField2D& field, float sharpness) {
    return transform_field(field,
                           [sharpness](float value) { return ridge_profile(value, sharpness); });
}

ScalarField2D terrace_unit_field(const ScalarField2D& field, std::uint32_t steps, float blend) {
    return transform_field(
        field, [steps, blend](float value) { return terrace_unit(value, steps, blend); });
}

ScalarFieldDistribution summarize_scalar_field_distribution(std::span<const float> values) {
    ScalarFieldDistribution result{
        .stats = summarize_scalar_field(values),
    };
    if (values.empty()) {
        return result;
    }

    std::vector<float> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    result.p01 = percentile_from_sorted(sorted, 0.01F);
    result.p05 = percentile_from_sorted(sorted, 0.05F);
    result.p10 = percentile_from_sorted(sorted, 0.10F);
    result.p25 = percentile_from_sorted(sorted, 0.25F);
    result.p50 = percentile_from_sorted(sorted, 0.50F);
    result.p75 = percentile_from_sorted(sorted, 0.75F);
    result.p90 = percentile_from_sorted(sorted, 0.90F);
    result.p95 = percentile_from_sorted(sorted, 0.95F);
    result.p99 = percentile_from_sorted(sorted, 0.99F);
    return result;
}

ScalarFieldDistribution summarize_scalar_field_distribution(const ScalarField2D& field) {
    return summarize_scalar_field_distribution(field.values());
}

ScalarField2D percentile_remap_field(const ScalarField2D& field, float low_percentile,
                                     float high_percentile, float out_min, float out_max) {
    require_percentile_range(low_percentile, high_percentile);
    const std::span<const float> values = field.values();
    if (values.empty()) {
        throw std::runtime_error("procedural percentile remap requires at least one sample");
    }

    std::vector<float> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const float in_min = percentile_from_sorted(sorted, low_percentile);
    const float in_max = percentile_from_sorted(sorted, high_percentile);
    if (in_min == in_max) {
        throw std::runtime_error("procedural percentile remap input span must be non-zero");
    }
    return remap_field(field, in_min, in_max, out_min, out_max);
}

ScalarField2D add_fields(const ScalarField2D& lhs, const ScalarField2D& rhs) {
    return combine_fields(lhs, rhs, [](float a, float b) { return a + b; });
}

ScalarField2D subtract_fields(const ScalarField2D& lhs, const ScalarField2D& rhs) {
    return combine_fields(lhs, rhs, [](float a, float b) { return a - b; });
}

ScalarField2D multiply_fields(const ScalarField2D& lhs, const ScalarField2D& rhs) {
    return combine_fields(lhs, rhs, [](float a, float b) { return a * b; });
}

ScalarField2D min_fields(const ScalarField2D& lhs, const ScalarField2D& rhs) {
    return combine_fields(lhs, rhs, [](float a, float b) { return std::min(a, b); });
}

ScalarField2D max_fields(const ScalarField2D& lhs, const ScalarField2D& rhs) {
    return combine_fields(lhs, rhs, [](float a, float b) { return std::max(a, b); });
}

ScalarField2D blend_fields(const ScalarField2D& lhs, const ScalarField2D& rhs,
                           const ScalarField2D& mask) {
    require_same_desc(lhs, rhs);
    require_same_desc(lhs, mask);
    ScalarField2D result(lhs.desc());
    const std::span<const float> lhs_values = lhs.values();
    const std::span<const float> rhs_values = rhs.values();
    const std::span<const float> mask_values = mask.values();
    std::span<float> target = result.values();
    for (std::size_t index = 0; index < lhs_values.size(); ++index) {
        target[index] = lerp(lhs_values[index], rhs_values[index], saturate(mask_values[index]));
    }
    return result;
}

SlopeCurvature2D compute_slope_curvature(const ScalarField2D& field) {
    const Grid2DDesc& desc = field.desc();
    SlopeCurvature2D result{
        .slope = ScalarField2D(desc, 0.0F),
        .curvature = ScalarField2D(desc, 0.0F),
    };

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::uint32_t x0 = x == 0U ? x : x - 1U;
            const std::uint32_t x1 = x + 1U >= desc.width ? x : x + 1U;
            const std::uint32_t y0 = y == 0U ? y : y - 1U;
            const std::uint32_t y1 = y + 1U >= desc.height ? y : y + 1U;
            const float span_x = static_cast<float>(x1 - x0) * desc.cell_size;
            const float span_y = static_cast<float>(y1 - y0) * desc.cell_size;
            const float dhdx = span_x == 0.0F ? 0.0F : (field.at(x1, y) - field.at(x0, y)) / span_x;
            const float dhdy = span_y == 0.0F ? 0.0F : (field.at(x, y1) - field.at(x, y0)) / span_y;

            const float slope = std::sqrt((dhdx * dhdx) + (dhdy * dhdy));
            result.slope.at(x, y) = slope;
            result.max_slope = std::max(result.max_slope, slope);

            const float center = field.at(x, y);
            float neighbor_sum = 0.0F;
            float neighbor_count = 0.0F;
            if (x > 0U) {
                neighbor_sum += field.at(x - 1U, y);
                neighbor_count += 1.0F;
            }
            if (x + 1U < desc.width) {
                neighbor_sum += field.at(x + 1U, y);
                neighbor_count += 1.0F;
            }
            if (y > 0U) {
                neighbor_sum += field.at(x, y - 1U);
                neighbor_count += 1.0F;
            }
            if (y + 1U < desc.height) {
                neighbor_sum += field.at(x, y + 1U);
                neighbor_count += 1.0F;
            }

            const float curvature =
                neighbor_count == 0.0F ? 0.0F : ((neighbor_sum / neighbor_count) - center);
            result.curvature.at(x, y) = curvature;
            result.max_abs_curvature = std::max(result.max_abs_curvature, std::abs(curvature));
        }
    }

    return result;
}

LocalRelief2D compute_local_relief(const ScalarField2D& field, std::uint32_t radius_samples) {
    const Grid2DDesc& desc = field.desc();
    LocalRelief2D result{
        .local_min = ScalarField2D(desc, 0.0F),
        .local_max = ScalarField2D(desc, 0.0F),
        .local_mean = ScalarField2D(desc, 0.0F),
        .local_span = ScalarField2D(desc, 0.0F),
    };

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        const std::uint32_t y0 = radius_samples > y ? 0U : y - radius_samples;
        const std::uint32_t y1 = y + std::min(radius_samples, (desc.height - 1U) - y);
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::uint32_t x0 = radius_samples > x ? 0U : x - radius_samples;
            const std::uint32_t x1 = x + std::min(radius_samples, (desc.width - 1U) - x);

            float local_min = field.at(x, y);
            float local_max = field.at(x, y);
            float local_sum = 0.0F;
            float local_count = 0.0F;
            for (std::uint32_t ny = y0; ny <= y1; ++ny) {
                for (std::uint32_t nx = x0; nx <= x1; ++nx) {
                    const float value = field.at(nx, ny);
                    local_min = std::min(local_min, value);
                    local_max = std::max(local_max, value);
                    local_sum += value;
                    local_count += 1.0F;
                }
            }

            result.local_min.at(x, y) = local_min;
            result.local_max.at(x, y) = local_max;
            result.local_mean.at(x, y) =
                local_count <= 0.0F ? field.at(x, y) : local_sum / local_count;
            result.local_span.at(x, y) = std::max(local_max - local_min, 0.0F);
        }
    }

    return result;
}

void normalize_to_unit(ScalarField2D& field) {
    const ScalarFieldStats stats = field.summarize();
    if (stats.sample_count == 0 || stats.span <= 0.0F) {
        field.fill(0.0F);
        return;
    }

    for (float& value : field.values()) {
        value = saturate((value - stats.min) / stats.span);
    }
}

} // namespace cubey::procedural
