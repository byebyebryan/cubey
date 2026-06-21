#include "terrain_generator.h"

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    const cubey::projects::terrain::TerrainRegionConfig config{};
    const cubey::projects::terrain::TerrainRegionProduct product =
        cubey::projects::terrain::generate_terrain_region(config);

    require(product.fields.desc().width == cubey::projects::terrain::kTerrainDefaultGridSize,
            "terrain product should use default grid width");
    require(product.fields.desc().height == cubey::projects::terrain::kTerrainDefaultGridSize,
            "terrain product should use default grid height");
    require(product.config.recipe_id == "temperate-mountain-river",
            "terrain product should preserve recipe id");
    require(product.fields.empty(), "initial terrain scaffold should not emit fields yet");
    return 0;
}
