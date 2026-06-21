#pragma once

#include "terrain_product.h"

namespace cubey::projects::terrain {

[[nodiscard]] TerrainRegionProduct generate_terrain_region(const TerrainRegionConfig& config);

} // namespace cubey::projects::terrain
