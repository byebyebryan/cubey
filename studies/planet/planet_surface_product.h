#pragma once

#include <cubey/procedural/artifact_cache.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::planet {

struct PlanetSurfaceProductConfig {
    std::uint32_t extent = 512U;
    std::uint64_t seed = 1337U;
};

struct PlanetSurfaceProduct {
    PlanetSurfaceProductConfig config{};
    std::vector<std::uint8_t> rgba{};
    std::uint64_t content_hash = 0U;
    bool cache_hit = false;
};

[[nodiscard]] cubey::procedural::ProceduralArtifactRecipe
planet_surface_product_recipe(const PlanetSurfaceProductConfig& config);
[[nodiscard]] PlanetSurfaceProduct
prepare_planet_surface_product(cubey::procedural::ProceduralArtifactCache& cache,
                               const PlanetSurfaceProductConfig& config);

} // namespace cubey::projects::planet
