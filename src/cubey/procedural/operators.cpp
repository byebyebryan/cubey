#include <cubey/procedural/operators.h>

#include <algorithm>
#include <cmath>

namespace cubey::procedural {

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
            const float dhdx =
                span_x == 0.0F ? 0.0F : (field.at(x1, y) - field.at(x0, y)) / span_x;
            const float dhdy =
                span_y == 0.0F ? 0.0F : (field.at(x, y1) - field.at(x, y0)) / span_y;

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
        const std::uint32_t y1 =
            y + std::min(radius_samples, (desc.height - 1U) - y);
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const std::uint32_t x0 = radius_samples > x ? 0U : x - radius_samples;
            const std::uint32_t x1 =
                x + std::min(radius_samples, (desc.width - 1U) - x);

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
