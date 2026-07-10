#pragma once

#include <cubey/procedural/field_2d.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainLandscapeGraph {
    cubey::procedural::ScalarField2D routing_surface_m{};
    cubey::procedural::ScalarField2D breach_mask{};
    cubey::procedural::ScalarField2D drainage_area_m2{};
    cubey::procedural::ScalarField2D flow_direction_x{};
    cubey::procedural::ScalarField2D flow_direction_z{};
    cubey::procedural::ScalarField2D slope_correction{};
    std::vector<int> receiver{};
    std::vector<std::size_t> upstream_to_downstream{};
    std::vector<std::size_t> downstream_to_upstream{};
    double total_input_area_m2 = 0.0;
    double terminal_outflow_area_m2 = 0.0;
    std::size_t unresolved_sink_count = 0U;
};

[[nodiscard]] TerrainLandscapeGraph
build_terrain_landscape_graph(const cubey::procedural::ScalarField2D& height_m, std::uint64_t seed,
                              std::uint32_t multigrid_level);

[[nodiscard]] cubey::procedural::ScalarField2D
downsample_terrain_landscape_field(const cubey::procedural::ScalarField2D& field);

[[nodiscard]] cubey::procedural::ScalarField2D
upsample_terrain_landscape_field(const cubey::procedural::ScalarField2D& field, std::uint64_t seed,
                                 std::uint32_t multigrid_level, float jitter_cells = 0.25F);

[[nodiscard]] float
terrain_landscape_basin_discontinuity_coverage(const cubey::procedural::ScalarField2D& height_m,
                                               const TerrainLandscapeGraph& graph,
                                               float minimum_excess_m);

} // namespace cubey::projects::terrain
