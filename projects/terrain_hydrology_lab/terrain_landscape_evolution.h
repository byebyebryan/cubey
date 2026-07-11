#pragma once

#include "terrain_landscape_graph.h"

#include <cubey/procedural/field_2d.h>

#include <cstdint>

namespace cubey::projects::terrain_hydrology_lab {

struct TerrainLandscapeEvolutionConfig {
    std::uint64_t seed = 0U;
    double age_years = 1.6e6;
    float stream_power_coefficient = 2.0e-5F;
    float stream_power_area_exponent = 0.4F;
    float hillslope_coefficient = 0.1F;
    float hack_constant = 1.5F;
    float hack_exponent = 0.6F;
    float thermal_coefficient = 1.0e-3F;
    float critical_slope = 0.577350269F;
    std::uint32_t multigrid_levels = 4U;
    std::uint32_t iterations_per_level = 6U;
    float relaxation = 0.25F;
    float upsample_jitter_cells = 0.25F;
    std::uint32_t altitude_correction_iterations = 50U;
    float altitude_correction_learning_rate = 0.01F;
};

struct TerrainLandscapeEvolutionResult {
    cubey::procedural::ScalarField2D uplift_rate_m_per_year{};
    cubey::procedural::ScalarField2D process_drainage_area_m2{};
    cubey::procedural::ScalarField2D process_flow_direction_x{};
    cubey::procedural::ScalarField2D process_flow_direction_z{};
    cubey::procedural::ScalarField2D process_breach_mask{};
    cubey::procedural::ScalarField2D fluvial_advection_rate_m_per_year{};
    cubey::procedural::ScalarField2D hillslope_advection_rate_m_per_year{};
    cubey::procedural::ScalarField2D thermal_active_mask{};
    cubey::procedural::ScalarField2D analytical_height_m{};
    cubey::procedural::ScalarField2D altitude_correction_delta_m{};
    cubey::procedural::ScalarField2D process_delta_m{};
    cubey::procedural::ScalarField2D height_m{};
    std::size_t unresolved_sink_count = 0U;
    float basin_discontinuity_coverage = 0.0F;
};

[[nodiscard]] TerrainLandscapeEvolutionResult
evolve_terrain_landscape(const cubey::procedural::ScalarField2D& initial_height_m,
                         const cubey::procedural::ScalarField2D& uplift_rate_m_per_year,
                         const TerrainLandscapeEvolutionConfig& config = {});

} // namespace cubey::projects::terrain_hydrology_lab
