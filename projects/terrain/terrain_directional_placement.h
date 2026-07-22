#pragma once

#include "terrain_height_source.h"

#include <cubey/render/terrain_directional_placement.h>

namespace cubey::projects::terrain {

using cubey::render::TerrainDirectionalPlacementPlan;
using cubey::render::TerrainDirectionalPlacementRequest;
using cubey::render::TerrainDirectionalSectorSample;
using cubey::render::evaluate_terrain_directional_placement;
using cubey::render::plan_terrain_directional_placement;
using cubey::render::terrain_directional_placement_maximum_refinement_offset_m;
using cubey::render::validate_terrain_directional_placement_request;

} // namespace cubey::projects::terrain
