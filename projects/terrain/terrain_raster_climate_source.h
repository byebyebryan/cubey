#pragma once

// Compatibility facade; source validation belongs to cubey::asset.
#include <cubey/asset/terrain_raster_climate_source.h>

namespace cubey::projects::terrain {
using cubey::asset::TerrainClimateSample;
using cubey::asset::TerrainHeightSource;
using cubey::asset::TerrainHeightSourceBounds;
using cubey::asset::TerrainHeightSourceMetadata;
using cubey::asset::TerrainQuery;
using cubey::asset::TerrainRasterClimateMetadata;
using cubey::asset::TerrainRasterClimateSource;
using cubey::asset::TerrainRasterHeightSource;
using cubey::asset::validate_terrain_climate_binding;
} // namespace cubey::projects::terrain
