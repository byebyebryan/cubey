#include "terrain_generator.h"

namespace cubey::projects::terrain {

TerrainRegionProduct generate_terrain_region(const TerrainRegionConfig& config) {
    TerrainRegionProduct product = make_empty_terrain_region_product(config);
    product.summary = summarize_terrain_region_product(product.config, product.fields);
    return product;
}

} // namespace cubey::projects::terrain
