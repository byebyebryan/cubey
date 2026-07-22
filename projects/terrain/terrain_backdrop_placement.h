#pragma once

#include "terrain_backdrop_profile.h"
#include "terrain_placement_mode.h"

#include <cubey/render/terrain_backdrop_placement.h>

namespace cubey::projects::terrain {

using cubey::render::TerrainBackdropPlacementPlan;
using cubey::render::TerrainBackdropPlacementRequest;
using cubey::render::plan_terrain_backdrop_placement;
using cubey::render::terrain_backdrop_centered_search_support_radius;
using cubey::render::terrain_backdrop_raw_sample_focus;
using cubey::render::terrain_backdrop_selected_support_radius;

} // namespace cubey::projects::terrain
