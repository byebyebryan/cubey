#pragma once

#include <cubey/terrain/terrain_backdrop_placement.h>
#include <cubey/terrain/terrain_backdrop_product.h>

#include <cstdint>
#include <filesystem>

namespace cubey::terrain {

struct TerrainRasterBackdropPreparationRequest {
    std::filesystem::path heightfield_path{};
    TerrainBackdropPlacementRequest placement{};
    std::uint32_t render_stride = 3U;
};

struct PreparedTerrainBackdropProduct {
    TerrainBackdropPlacementPlan placement{};
    TerrainBackdropProduct product{};
    float baked_foreground_height_m = 0.0F;
};

[[nodiscard]] PreparedTerrainBackdropProduct
prepare_raster_terrain_backdrop_product(const TerrainRasterBackdropPreparationRequest& request);

} // namespace cubey::terrain
