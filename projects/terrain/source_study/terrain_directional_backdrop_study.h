#pragma once

#include "terrain_backdrop_stage.h"
#include "terrain_directional_placement.h"
#include "terrain_directional_relief.h"
#include "terrain_radial_relief.h"

#include <optional>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainDirectionalBackdropLane : std::uint8_t {
    HardCut,
    ContinuousCurrent,
    Placement,
    Shaped,
    ExpandedShaped,
    ExpandedRadial,
    CachedRadial,
};

struct TerrainDirectionalBackdropStageParameters {
    float focus_height_m = 20.0F;
    float orbit_min_radius_m = 50.0F;
    float orbit_default_radius_m = 100.0F;
    float orbit_max_radius_m = 250.0F;
    float orbit_min_elevation_radians = 0.0F;
    float orbit_default_elevation_radians = 8.0F * 0.01745329251994329577F;
    float orbit_max_elevation_radians = 30.0F * 0.01745329251994329577F;
};

[[nodiscard]] std::string_view
terrain_directional_backdrop_lane_name(TerrainDirectionalBackdropLane lane) noexcept;
[[nodiscard]] TerrainDirectionalBackdropLane
terrain_directional_backdrop_lane_from_name(std::string_view name);

[[nodiscard]] TerrainDirectionalPlacementRequest directional_backdrop_placement_request();
[[nodiscard]] TerrainDirectionalReliefParameters
expanded_directional_backdrop_relief_parameters(const TerrainDirectionalPlacementPlan& placement);
[[nodiscard]] TerrainRadialReliefParameters
expanded_radial_backdrop_relief_parameters(const TerrainDirectionalPlacementPlan& placement);
[[nodiscard]] std::uint32_t
cached_radial_backdrop_render_stride(std::optional<std::uint32_t> requested_stride = std::nullopt);
[[nodiscard]] constexpr float expanded_backdrop_outer_radius_m() noexcept {
    return 32'768.0F;
}

[[nodiscard]] TerrainBackdropStagePlan make_directional_backdrop_stage_plan(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementPlan& placement,
    float vertical_scale = 1.0F, TerrainDirectionalBackdropStageParameters parameters = {});

} // namespace cubey::projects::terrain
