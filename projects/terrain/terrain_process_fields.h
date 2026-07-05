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

struct TerrainProcessGullyDiagnosticConfig {
    float support_start = 0.12F;
    float support_full = 0.68F;
    float slope_start = 0.08F;
    float slope_full = 0.48F;
    float relief_start_m = 18.0F;
    float relief_full_m = 280.0F;
    int mask_blur_iterations = 2;
    int mask_spread_iterations = 1;
    float spread_decay_per_cell = 0.72F;
    float base_delta_limit_m = 4.0F;
    float relief_delta_fraction = 0.18F;
    float max_delta_m = 78.0F;
};

struct TerrainProcessGullyDiagnosticFields {
    cubey::procedural::ScalarField2D erosion_delta_m{};
    cubey::procedural::ScalarField2D gully_mask{};
    cubey::procedural::ScalarField2D crease_proxy{};
    cubey::procedural::ScalarField2D post_erosion_height_m{};
};

struct TerrainProcessThermalTalusConfig {
    float support_start = 0.24F;
    float support_full = 0.78F;
    float talus_slope = 0.72F;
    int iterations = 5;
    float transfer_fraction = 0.28F;
    float base_transfer_limit_m = 6.0F;
    float relief_transfer_fraction = 0.06F;
    float max_total_erosion_m = 82.0F;
};

struct TerrainProcessThermalTalusFields {
    cubey::procedural::ScalarField2D thermal_erosion_delta_m{};
    cubey::procedural::ScalarField2D talus_deposition_m{};
    cubey::procedural::ScalarField2D slope_instability{};
    cubey::procedural::ScalarField2D post_erosion_height_m{};
};

struct TerrainProcessMountainMorphologyConfig {
    float support_start = 0.18F;
    float support_full = 0.76F;
    float chain_start = 0.24F;
    float chain_full = 0.86F;
    float slope_start = 0.12F;
    float slope_full = 0.62F;
    float relief_start_m = 82.0F;
    float relief_full_m = 560.0F;
    int crease_blur_iterations = 1;
    int crease_spread_iterations = 1;
    float spread_decay_per_cell = 0.52F;
    float base_delta_limit_m = 4.0F;
    float relief_delta_fraction = 0.10F;
    float max_delta_m = 86.0F;
};

struct TerrainProcessMountainMorphologyFields {
    cubey::procedural::ScalarField2D morphology_delta_m{};
    cubey::procedural::ScalarField2D crease_map{};
    cubey::procedural::ScalarField2D ridge_map{};
    cubey::procedural::ScalarField2D post_morphology_height_m{};
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

[[nodiscard]] TerrainProcessGullyDiagnosticFields compute_gully_erosion_diagnostic(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& slope,
    const cubey::procedural::ScalarField2D& curvature,
    const cubey::procedural::ScalarField2D& local_relief,
    const cubey::procedural::ScalarField2D& support,
    TerrainProcessGullyDiagnosticConfig config);

[[nodiscard]] TerrainProcessThermalTalusFields compute_thermal_talus_diagnostic(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& slope,
    const cubey::procedural::ScalarField2D& local_relief,
    const cubey::procedural::ScalarField2D& support,
    TerrainProcessThermalTalusConfig config);

[[nodiscard]] TerrainProcessMountainMorphologyFields compute_mountain_morphology(
    const cubey::procedural::ScalarField2D& height,
    const cubey::procedural::ScalarField2D& slope,
    const cubey::procedural::ScalarField2D& curvature,
    const cubey::procedural::ScalarField2D& local_relief,
    const cubey::procedural::ScalarField2D& support,
    const cubey::procedural::ScalarField2D& ridged_chain,
    const cubey::procedural::ScalarField2D& detail_weight,
    TerrainProcessMountainMorphologyConfig config);

} // namespace cubey::projects::terrain
