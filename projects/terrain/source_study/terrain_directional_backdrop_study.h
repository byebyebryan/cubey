#pragma once

#include "terrain_backdrop_stage.h"
#include "terrain_directional_placement.h"

#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainDirectionalBackdropLane : std::uint8_t {
    HardCut,
    ContinuousCurrent,
    Placement,
    Shaped,
};

[[nodiscard]] std::string_view
terrain_directional_backdrop_lane_name(TerrainDirectionalBackdropLane lane) noexcept;
[[nodiscard]] TerrainDirectionalBackdropLane
terrain_directional_backdrop_lane_from_name(std::string_view name);

[[nodiscard]] TerrainDirectionalPlacementRequest directional_backdrop_placement_request();

[[nodiscard]] TerrainBackdropStagePlan make_directional_backdrop_stage_plan(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementPlan& placement,
    float vertical_scale = 1.0F);

} // namespace cubey::projects::terrain
