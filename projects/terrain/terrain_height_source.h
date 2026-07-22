#pragma once

#include <cubey/asset/terrain_height_source.h>

namespace cubey::projects::terrain {

using TerrainHeightSource = cubey::asset::TerrainHeightSource;
using TerrainHeightSourceBounds = cubey::asset::TerrainHeightSourceBounds;
using TerrainHeightSourceMetadata = cubey::asset::TerrainHeightSourceMetadata;
using TerrainQuery = cubey::asset::TerrainQuery;
using TerrainSample = cubey::asset::TerrainSample;

using cubey::asset::terrain_height_source_bounds_center;
using cubey::asset::terrain_height_source_bounds_contains_disk;
using cubey::asset::validate_terrain_height_source_bounds;
using cubey::asset::validate_terrain_height_source_metadata;

} // namespace cubey::projects::terrain
