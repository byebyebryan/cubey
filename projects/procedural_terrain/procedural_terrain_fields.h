#pragma once

#include "procedural_terrain_config.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::procedural_terrain {

struct TerrainGridDesc {
    std::uint32_t version = 1;
    std::uint64_t seed = kTerrainDefaultSeed;
    std::uint32_t width = kTerrainDefaultGridWidth;
    std::uint32_t height = kTerrainDefaultGridHeight;
    float cell_size_m = kTerrainDefaultCellSizeMeters;
    float sea_level_m = kTerrainDefaultSeaLevelMeters;
    float origin_x_m = 0.0F;
    float origin_z_m = 0.0F;
};

struct TerrainMaterialMask {
    float sand = 0.0F;
    float rock = 0.0F;
    float vegetation = 0.0F;
    float sediment = 0.0F;
};

struct TerrainFieldData {
    TerrainGridDesc desc{};
    std::vector<float> height_m{};
    std::vector<float> water_depth_m{};
    std::vector<float> shore_sdf_m{};
    std::vector<float> slope{};
    std::vector<TerrainMaterialMask> material_masks{};
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    float max_water_depth_m = 0.0F;
    float max_abs_shore_sdf_m = 0.0F;

    [[nodiscard]] std::size_t sample_count() const;
    [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y) const;
};

[[nodiscard]] TerrainFieldData generate_terrain_fields(const TerrainConfig& config);

} // namespace cubey::projects::procedural_terrain
