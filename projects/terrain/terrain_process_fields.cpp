#include "terrain_process_fields.h"

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

} // namespace cubey::projects::terrain
