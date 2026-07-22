#pragma once

#include <cubey/render/terrain_shadow.h>

namespace cubey::projects::terrain {

using cubey::render::TerrainShadowCacheState;
using cubey::render::TerrainShadowProductBounds;
using cubey::render::TerrainShadowProjection;
using cubey::render::invalidate_terrain_shadow_cache;
using cubey::render::kTerrainShadowDirectionThresholdRadians;
using cubey::render::kTerrainShadowMapExtent;
using cubey::render::terrain_shadow_light_above_horizon;
using cubey::render::terrain_shadow_projection;
using cubey::render::terrain_shadow_update_required;
using cubey::render::update_terrain_shadow_cache;

} // namespace cubey::projects::terrain
