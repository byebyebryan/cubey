#include "terrain_generator.h"

#include <cmath>
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
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldHeightM),
            "terrain product should emit height");
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldBaseElevation),
            "terrain product should emit base elevation");
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldBroadRelief),
            "terrain product should emit broad relief");
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldRidgeUplift),
            "terrain product should emit ridge uplift");
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldDetailResidual),
            "terrain product should emit detail residual");
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldSlope),
            "terrain product should emit slope");
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldCurvature),
            "terrain product should emit curvature");
    require(product.fields.has_field(cubey::projects::terrain::kTerrainFieldLocalRelief),
            "terrain product should emit local relief");
    require(product.summary.height.sample_count ==
                product.fields.desc().width * product.fields.desc().height,
            "terrain summary should cover height samples");
    require(product.summary.height.span > 200.0F, "terrain source fields should produce relief");
    require(product.summary.slope.max > 0.01F, "terrain should have nonzero slope");
    require(std::isfinite(product.summary.height.min) && std::isfinite(product.summary.height.max),
            "terrain summary should be finite");
    return 0;
}
