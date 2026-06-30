#pragma once

#include <cubey/procedural/field_2d.h>

namespace cubey::projects::terrain {

struct TerrainProcessLoweringLimit {
    float base_limit_m = 0.0F;
    float relief_fraction = 0.0F;
    float max_total_m = 0.0F;
};

struct TerrainProcessSplitLoweringFields {
    cubey::procedural::ScalarField2D primary{};
    cubey::procedural::ScalarField2D secondary{};
    cubey::procedural::ScalarField2D total{};
};

[[nodiscard]] cubey::procedural::ScalarField2D spread_max_decay_field(
    const cubey::procedural::ScalarField2D& source, int iterations, float decay_per_cell);

[[nodiscard]] TerrainProcessSplitLoweringFields clamp_split_lowering_to_relief(
    const cubey::procedural::ScalarField2D& primary,
    const cubey::procedural::ScalarField2D& secondary,
    const cubey::procedural::ScalarField2D& local_relief,
    TerrainProcessLoweringLimit limit);

[[nodiscard]] cubey::procedural::ScalarField2D subtract_lowering_from_height(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& lowering);

} // namespace cubey::projects::terrain
