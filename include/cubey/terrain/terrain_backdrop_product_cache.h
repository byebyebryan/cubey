#pragma once

#include <cubey/procedural/artifact_cache.h>
#include <cubey/terrain/terrain_backdrop_product.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::terrain {

inline constexpr std::string_view kTerrainBackdropProductCacheFormulaVersion =
    "terrain-backdrop-product-cache-v1";

struct TerrainBackdropProductRecipeContext {
    std::string source_content_sha256{};
    std::string climate_content_sha256{};
    std::string surface_formula_version{};
    std::uint64_t surface_parameter_hash = 0U;
    std::uint64_t placement_parameter_hash = 0U;
};

struct DecodedTerrainBackdropProduct {
    TerrainBackdropProduct product{};
    std::vector<std::uint8_t> auxiliary{};
};

[[nodiscard]] cubey::procedural::ProceduralArtifactRecipe
terrain_backdrop_product_cache_recipe(const TerrainBackdropProductRequest& request,
                                      const TerrainBackdropSourceInfo& source,
                                      const TerrainBackdropProductRecipeContext& context);

[[nodiscard]] std::vector<std::uint8_t>
encode_terrain_backdrop_product(const TerrainBackdropProduct& product,
                                std::span<const std::uint8_t> auxiliary = {});
[[nodiscard]] DecodedTerrainBackdropProduct
decode_terrain_backdrop_product(std::span<const std::uint8_t> payload);

} // namespace cubey::terrain
