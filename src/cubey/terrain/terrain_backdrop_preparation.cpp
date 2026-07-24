#include <cubey/terrain/terrain_backdrop_preparation.h>

#include <cubey/asset/terrain_raster_height_source.h>

#include <stdexcept>
#include <utility>

namespace cubey::terrain {

PreparedTerrainBackdropProduct
prepare_raster_terrain_backdrop_product(const TerrainRasterBackdropPreparationRequest& request) {
    if (request.heightfield_path.empty()) {
        throw std::runtime_error("terrain backdrop heightfield path is empty");
    }
    if (!std::filesystem::exists(request.heightfield_path)) {
        throw std::runtime_error("terrain backdrop heightfield does not exist: " +
                                 request.heightfield_path.string());
    }

    const cubey::asset::TerrainRasterHeightSource source(request.heightfield_path);
    TerrainBackdropPlacementPlan placement =
        plan_terrain_backdrop_placement(source, source.bounds(), request.placement);
    TerrainBackdropProduct product = make_terrain_backdrop_product(
        terrain_backdrop_v1_product_request(placement.stage, request.render_stride), source);
    const float baked_foreground_height_m =
        placement.stage.target_height_m - placement.stage.source_center_height_m;
    return {
        .placement = std::move(placement),
        .product = std::move(product),
        .baked_foreground_height_m = baked_foreground_height_m,
    };
}

} // namespace cubey::terrain
