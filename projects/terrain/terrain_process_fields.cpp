#include "terrain_process_fields.h"

#include <cubey/procedural/operators.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

void require_same_grid(const cubey::procedural::ScalarField2D& lhs,
                       const cubey::procedural::ScalarField2D& rhs,
                       const char* message) {
    if (!cubey::procedural::same_grid_desc(lhs.desc(), rhs.desc())) {
        throw std::runtime_error(message);
    }
}

void validate_lowering_limit(TerrainProcessLoweringLimit limit) {
    if (!std::isfinite(limit.base_limit_m) || limit.base_limit_m < 0.0F) {
        throw std::runtime_error("terrain process lowering base limit must be finite and non-negative");
    }
    if (!std::isfinite(limit.relief_fraction) || limit.relief_fraction < 0.0F) {
        throw std::runtime_error(
            "terrain process lowering relief fraction must be finite and non-negative");
    }
    if (!std::isfinite(limit.max_total_m) || limit.max_total_m < 0.0F) {
        throw std::runtime_error("terrain process lowering max limit must be finite and non-negative");
    }
}

void validate_gully_config(TerrainProcessGullyDiagnosticConfig config) {
    if (!std::isfinite(config.support_start) || !std::isfinite(config.support_full) ||
        config.support_start >= config.support_full) {
        throw std::runtime_error("terrain gully diagnostic support range must be increasing");
    }
    if (!std::isfinite(config.slope_start) || !std::isfinite(config.slope_full) ||
        config.slope_start >= config.slope_full) {
        throw std::runtime_error("terrain gully diagnostic slope range must be increasing");
    }
    if (!std::isfinite(config.relief_start_m) || !std::isfinite(config.relief_full_m) ||
        config.relief_start_m >= config.relief_full_m) {
        throw std::runtime_error("terrain gully diagnostic relief range must be increasing");
    }
    if (config.mask_blur_iterations < 0 || config.mask_spread_iterations < 0) {
        throw std::runtime_error("terrain gully diagnostic iterations must be non-negative");
    }
    if (!std::isfinite(config.spread_decay_per_cell) || config.spread_decay_per_cell < 0.0F ||
        config.spread_decay_per_cell > 1.0F) {
        throw std::runtime_error("terrain gully diagnostic spread decay must be in [0, 1]");
    }
    if (!std::isfinite(config.base_delta_limit_m) || config.base_delta_limit_m < 0.0F ||
        !std::isfinite(config.relief_delta_fraction) || config.relief_delta_fraction < 0.0F ||
        !std::isfinite(config.max_delta_m) || config.max_delta_m < 0.0F) {
        throw std::runtime_error(
            "terrain gully diagnostic delta limits must be finite and non-negative");
    }
}

} // namespace

cubey::procedural::ScalarField2D spread_max_decay_field(
    const cubey::procedural::ScalarField2D& source, int iterations, float decay_per_cell) {
    if (iterations < 0) {
        throw std::runtime_error("terrain process spread iterations must be non-negative");
    }
    if (!std::isfinite(decay_per_cell) || decay_per_cell < 0.0F || decay_per_cell > 1.0F) {
        throw std::runtime_error("terrain process spread decay must be finite and in [0, 1]");
    }

    cubey::procedural::ScalarField2D current = source;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        cubey::procedural::ScalarField2D next = current;
        const cubey::procedural::Grid2DDesc& desc = current.desc();
        for (std::uint32_t y = 0; y < desc.height; ++y) {
            for (std::uint32_t x = 0; x < desc.width; ++x) {
                float best = current.at(x, y);
                for (int oy = -1; oy <= 1; ++oy) {
                    const int sy = static_cast<int>(y) + oy;
                    if (sy < 0 || sy >= static_cast<int>(desc.height)) {
                        continue;
                    }
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int sx = static_cast<int>(x) + ox;
                        if (sx < 0 || sx >= static_cast<int>(desc.width) ||
                            (ox == 0 && oy == 0)) {
                            continue;
                        }
                        best = std::max(
                            best, current.at(static_cast<std::uint32_t>(sx),
                                             static_cast<std::uint32_t>(sy)) *
                                      decay_per_cell);
                    }
                }
                next.at(x, y) = best;
            }
        }
        current = std::move(next);
    }
    return current;
}

TerrainProcessSplitLoweringFields clamp_split_lowering_to_relief(
    const cubey::procedural::ScalarField2D& primary,
    const cubey::procedural::ScalarField2D& secondary,
    const cubey::procedural::ScalarField2D& local_relief,
    TerrainProcessLoweringLimit limit) {
    require_same_grid(primary, secondary,
                      "terrain process split lowering fields must use the same grid");
    require_same_grid(primary, local_relief,
                      "terrain process lowering relief field must use the same grid");
    validate_lowering_limit(limit);

    const cubey::procedural::Grid2DDesc& desc = primary.desc();
    TerrainProcessSplitLoweringFields fields{
        .primary = cubey::procedural::ScalarField2D(desc, 0.0F),
        .secondary = cubey::procedural::ScalarField2D(desc, 0.0F),
        .total = cubey::procedural::ScalarField2D(desc, 0.0F),
    };

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            float primary_lowering = std::max(primary.at(x, y), 0.0F);
            float secondary_lowering = std::max(secondary.at(x, y), 0.0F);
            const float sample_relief = std::max(local_relief.at(x, y), 0.0F);
            const float lowering_limit =
                std::min(limit.max_total_m,
                         limit.base_limit_m + (sample_relief * limit.relief_fraction));
            const float raw_total = primary_lowering + secondary_lowering;
            if (raw_total > lowering_limit && raw_total > 0.0F) {
                const float scale = lowering_limit / raw_total;
                primary_lowering *= scale;
                secondary_lowering *= scale;
            }

            fields.primary.at(x, y) = primary_lowering;
            fields.secondary.at(x, y) = secondary_lowering;
            fields.total.at(x, y) = primary_lowering + secondary_lowering;
        }
    }

    return fields;
}

cubey::procedural::ScalarField2D subtract_lowering_from_height(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& lowering) {
    require_same_grid(height, lowering, "terrain process lowering field must use the height grid");

    cubey::procedural::ScalarField2D result = height;
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            result.at(x, y) = height.at(x, y) - lowering.at(x, y);
        }
    }
    return result;
}

TerrainProcessGullyDiagnosticFields compute_gully_erosion_diagnostic(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& slope,
    const cubey::procedural::ScalarField2D& curvature,
    const cubey::procedural::ScalarField2D& local_relief,
    const cubey::procedural::ScalarField2D& support,
    TerrainProcessGullyDiagnosticConfig config) {
    require_same_grid(height, slope, "terrain gully diagnostic slope must use the height grid");
    require_same_grid(height, curvature,
                      "terrain gully diagnostic curvature must use the height grid");
    require_same_grid(height, local_relief,
                      "terrain gully diagnostic relief must use the height grid");
    require_same_grid(height, support, "terrain gully diagnostic support must use the height grid");
    validate_gully_config(config);

    const cubey::procedural::Grid2DDesc& desc = height.desc();
    TerrainProcessGullyDiagnosticFields fields{
        .erosion_delta_m = cubey::procedural::ScalarField2D(desc, 0.0F),
        .gully_mask = cubey::procedural::ScalarField2D(desc, 0.0F),
        .crease_proxy = cubey::procedural::ScalarField2D(desc, 0.0F),
        .post_erosion_height_m = height,
    };

    cubey::procedural::ScalarField2D abs_curvature(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            abs_curvature.at(x, y) = std::abs(curvature.at(x, y));
        }
    }

    const cubey::procedural::ScalarFieldDistribution curvature_distribution =
        cubey::procedural::summarize_scalar_field_distribution(abs_curvature);
    const float curvature_scale = std::max(curvature_distribution.p95, 0.001F);

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float curvature_t =
                cubey::procedural::saturate(abs_curvature.at(x, y) / curvature_scale);
            const float slope_gate =
                cubey::procedural::smoothstep(config.slope_start, config.slope_full,
                                              slope.at(x, y));
            const float relief_gate =
                cubey::procedural::smoothstep(config.relief_start_m, config.relief_full_m,
                                              local_relief.at(x, y));
            const float support_gate =
                cubey::procedural::smoothstep(config.support_start, config.support_full,
                                              support.at(x, y));
            fields.crease_proxy.at(x, y) =
                cubey::procedural::saturate(std::pow(curvature_t, 0.85F) * slope_gate *
                                            relief_gate * support_gate);
        }
    }

    cubey::procedural::ScalarField2D mask = fields.crease_proxy;
    for (int iteration = 0; iteration < config.mask_blur_iterations; ++iteration) {
        mask = cubey::procedural::box_blur_3x3(mask);
    }
    mask = spread_max_decay_field(mask, config.mask_spread_iterations,
                                  config.spread_decay_per_cell);
    fields.gully_mask = cubey::procedural::clamp_field(mask, 0.0F, 1.0F);

    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            const float slope_gate =
                cubey::procedural::smoothstep(config.slope_start, config.slope_full,
                                              slope.at(x, y));
            const float raw_delta =
                fields.gully_mask.at(x, y) * config.max_delta_m * (0.45F + (0.55F * slope_gate));
            const float relief_limit =
                config.base_delta_limit_m +
                (std::max(local_relief.at(x, y), 0.0F) * config.relief_delta_fraction);
            fields.erosion_delta_m.at(x, y) =
                std::min(raw_delta, std::min(config.max_delta_m, relief_limit));
        }
    }
    fields.post_erosion_height_m =
        subtract_lowering_from_height(height, fields.erosion_delta_m);

    return fields;
}

} // namespace cubey::projects::terrain
