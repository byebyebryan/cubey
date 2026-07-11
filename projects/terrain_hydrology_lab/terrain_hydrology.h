#pragma once

#include <cubey/procedural/field_2d.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::terrain_hydrology_lab {

struct TerrainFlowReceiver {
    int index = -1;
    float weight = 0.0F;
};

struct TerrainHydrologyResult {
    cubey::procedural::ScalarField2D routing_surface_m{};
    cubey::procedural::ScalarField2D routing_fill_delta_m{};
    cubey::procedural::ScalarField2D flow_direction_x{};
    cubey::procedural::ScalarField2D flow_direction_z{};
    cubey::procedural::ScalarField2D contributing_area_m2{};
    cubey::procedural::ScalarField2D stream_order{};
    cubey::procedural::ScalarField2D discharge_proxy{};
    cubey::procedural::ScalarField2D sink_mask{};
    cubey::procedural::ScalarField2D flow_boundary_mask{};
    std::vector<std::array<TerrainFlowReceiver, 2>> receivers{};
    std::vector<int> primary_receiver{};
    double total_input_area_m2 = 0.0;
    double terminal_outflow_area_m2 = 0.0;
};

[[nodiscard]] TerrainHydrologyResult
compute_regional_hydrology(const cubey::procedural::ScalarField2D& height_m,
                           std::uint32_t core_border_samples);

} // namespace cubey::projects::terrain_hydrology_lab
