#pragma once

#include <cubey/terrain/terrain_backdrop_placement.h>
#include <cubey/terrain/terrain_backdrop_product.h>
#include <cubey/terrain/terrain_backdrop_surface.h>

#include <cstdint>
#include <filesystem>

namespace cubey::terrain {

struct TerrainRasterBackdropPreparationRequest {
    std::filesystem::path heightfield_path{};
    TerrainBackdropPlacementRequest placement{};
    std::uint32_t render_stride = 3U;
    float foreground_footprint_radius_m = 0.0F;
};

struct PreparedTerrainBackdropProduct {
    TerrainBackdropPlacementPlan placement{};
    TerrainBackdropProduct product{};
    TerrainBackdropSurfaceEnvelope foreground_surface{};
    float baked_foreground_height_m = 0.0F;
};

[[nodiscard]] PreparedTerrainBackdropProduct
prepare_raster_terrain_backdrop_product(const TerrainRasterBackdropPreparationRequest& request);

} // namespace cubey::terrain
