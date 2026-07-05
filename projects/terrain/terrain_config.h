#pragma once

#include <cubey/procedural/field_2d.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::terrain {

inline constexpr std::uint32_t kTerrainDefaultGridSize = 257U;
inline constexpr std::uint32_t kTerrainMinGridSize = 17U;
inline constexpr std::uint32_t kTerrainMaxGridSize = 2049U;
inline constexpr float kTerrainDefaultCellSizeM = 32.0F;
inline constexpr std::uint64_t kTerrainDefaultSeed = 0x7465'7272'6169'6e01ULL;
inline constexpr std::uint32_t kTerrainGeneratorRevision = 32U;
inline constexpr std::string_view kTerrainRecipeTemperateMountainRiver =
    "temperate-mountain-river";
inline constexpr std::string_view kTerrainRecipeTemperateMountainRiverStress =
    "temperate-mountain-river-stress";
inline constexpr std::string_view kTerrainRecipeTemperateMountainRangeStress =
    "temperate-mountain-range-stress";

struct TerrainRegionConfig {
    std::uint32_t grid_width = kTerrainDefaultGridSize;
    std::uint32_t grid_height = kTerrainDefaultGridSize;
    float cell_size_m = kTerrainDefaultCellSizeM;
    float origin_x_m = 0.0F;
    float origin_y_m = 0.0F;
    std::uint64_t seed = kTerrainDefaultSeed;
    std::string recipe_id = std::string(kTerrainRecipeTemperateMountainRiver);
    std::uint32_t generator_revision = kTerrainGeneratorRevision;
};

inline void validate_terrain_region_config(const TerrainRegionConfig& config) {
    if (config.grid_width < kTerrainMinGridSize || config.grid_width > kTerrainMaxGridSize ||
        config.grid_height < kTerrainMinGridSize || config.grid_height > kTerrainMaxGridSize) {
        throw std::runtime_error("terrain grid dimensions must be in [17, 2049]");
    }
    if (!std::isfinite(config.cell_size_m) || config.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain cell size must be positive");
    }
    if (!std::isfinite(config.origin_x_m) || !std::isfinite(config.origin_y_m)) {
        throw std::runtime_error("terrain origin must be finite");
    }
    if (config.recipe_id.empty()) {
        throw std::runtime_error("terrain recipe id must be non-empty");
    }
    if (config.generator_revision == 0U) {
        throw std::runtime_error("terrain generator revision must be non-zero");
    }
}

[[nodiscard]] inline cubey::procedural::Grid2DDesc
terrain_region_grid_desc(const TerrainRegionConfig& config) {
    validate_terrain_region_config(config);
    return cubey::procedural::Grid2DDesc{
        .width = config.grid_width,
        .height = config.grid_height,
        .cell_size = config.cell_size_m,
        .origin_x = config.origin_x_m,
        .origin_y = config.origin_y_m,
    };
}

} // namespace cubey::projects::terrain
