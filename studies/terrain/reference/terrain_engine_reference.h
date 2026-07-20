#pragma once

#include <cubey/procedural/field_2d.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain_ref {

inline constexpr std::string_view kTerrainRefRecipeTerrainEngine = "terrain-engine-ref";
inline constexpr std::uint64_t kTerrainRefDefaultSeed = 0x7465'7272'6166'0001ULL;
inline constexpr float kTerrainEngineReferenceWaterHeightM = 100.0F;

struct TerrainEngineReferenceSeedComponents {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] bool is_terrain_engine_reference_recipe(std::string_view recipe_id);
[[nodiscard]] TerrainEngineReferenceSeedComponents
terrain_engine_reference_seed_components(std::uint64_t seed);
[[nodiscard]] float terrain_engine_reference_height(float world_x, float world_y,
                                                    std::uint64_t seed);
[[nodiscard]] cubey::procedural::ScalarField2D
terrain_engine_reference_height_field(cubey::procedural::Grid2DDesc desc, std::uint64_t seed);
[[nodiscard]] float terrain_engine_reference_normal_cos_v(float world_x, float world_y,
                                                          std::uint64_t seed);

} // namespace cubey::projects::terrain_ref
