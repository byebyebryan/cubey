#pragma once

#include <cubey/render/terrain_backdrop_stage.h>
#include <cubey/render/terrain_directional_placement.h>

namespace cubey::render {

struct TerrainFocusedBackdropStageParameters {
    float focus_height_m = 20.0F;
    float orbit_min_radius_m = 50.0F;
    float orbit_default_radius_m = 100.0F;
    float orbit_max_radius_m = 250.0F;
    float orbit_min_elevation_radians = 0.0F;
    float orbit_default_elevation_radians = 8.0F * 0.01745329251994329577F;
    float orbit_max_elevation_radians = 30.0F * 0.01745329251994329577F;
};

[[nodiscard]] TerrainBackdropStagePlan plan_terrain_focused_backdrop_stage(
    const TerrainHeightSource& source, const TerrainDirectionalPlacementPlan& placement,
    float vertical_scale = 1.0F, TerrainFocusedBackdropStageParameters parameters = {},
    TerrainDirectionalPlacementRequest placement_request = {});

} // namespace cubey::render
