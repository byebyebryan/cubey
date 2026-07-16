#pragma once

#include "terrain_backdrop_stage.h"
#include "terrain_directional_placement.h"
#include "terrain_radial_relief.h"

#include <cstdint>

namespace cubey::projects::terrain {

struct TerrainFocusedBackdropStageParameters {
    float focus_height_m = 20.0F;
    float orbit_min_radius_m = 50.0F;
    float orbit_default_radius_m = 100.0F;
    float orbit_max_radius_m = 250.0F;
    float orbit_min_elevation_radians = 0.0F;
    float orbit_default_elevation_radians = 8.0F * 0.01745329251994329577F;
    float orbit_max_elevation_radians = 30.0F * 0.01745329251994329577F;
};

struct TerrainRadialBackdropProfile {
    float outer_radius_m = 32'768.0F;
    float visible_inner_radius_m = 6'000.0F;
    std::uint32_t render_stride = 3U;
    TerrainFocusedBackdropStageParameters stage{
        .focus_height_m = 500.0F,
        .orbit_min_radius_m = 100.0F,
        .orbit_default_radius_m = 400.0F,
        .orbit_max_radius_m = 1'000.0F,
    };
};

[[nodiscard]] constexpr TerrainRadialBackdropProfile terrain_radial_backdrop_profile() noexcept {
    return {};
}

[[nodiscard]] TerrainRadialReliefParameters
terrain_radial_backdrop_relief_parameters(const TerrainDirectionalPlacementPlan& placement);

[[nodiscard]] TerrainBackdropStagePlan plan_terrain_focused_backdrop_stage(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementPlan& placement,
    float vertical_scale = 1.0F, TerrainFocusedBackdropStageParameters parameters = {});

} // namespace cubey::projects::terrain
