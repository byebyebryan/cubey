#pragma once

#include "procedural_terrain_config.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
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

[[nodiscard]] inline float terrain_grid_centered_offset_m(std::uint32_t index,
                                                          std::uint32_t count,
                                                          float cell_size_m) {
    if (index >= count) {
        throw std::runtime_error("terrain grid sample index is out of bounds");
    }
    return (static_cast<float>(index) - (static_cast<float>(count - 1U) * 0.5F)) * cell_size_m;
}

[[nodiscard]] inline float terrain_grid_sample_x_m(const TerrainGridDesc& desc,
                                                   std::uint32_t x) {
    return desc.origin_x_m + terrain_grid_centered_offset_m(x, desc.width, desc.cell_size_m);
}

[[nodiscard]] inline float terrain_grid_sample_z_m(const TerrainGridDesc& desc,
                                                   std::uint32_t y) {
    return desc.origin_z_m + terrain_grid_centered_offset_m(y, desc.height, desc.cell_size_m);
}

struct TerrainMaterialMask {
    float sand = 0.0F;
    float rock = 0.0F;
    float vegetation = 0.0F;
    float sediment = 0.0F;
};

struct TerrainHeightContributions {
    float coast_lift_m = 0.0F;
    float inland_lift_m = 0.0F;
    float broad_noise_m = 0.0F;
    float detail_noise_m = 0.0F;
    float foothills_m = 0.0F;
    float ridge_m = 0.0F;
    float broken_ridge_m = 0.0F;
    float valley_cut_m = 0.0F;
    float pre_relax_height_m = 0.0F;
    float relax_delta_m = 0.0F;
};

struct TerrainFieldData {
    TerrainGridDesc desc{};
    std::vector<float> height_m{};
    std::vector<float> water_depth_m{};
    std::vector<float> shore_sdf_m{};
    std::vector<float> slope{};
    std::vector<TerrainMaterialMask> material_masks{};
    std::vector<TerrainHeightContributions> height_contributions{};
    std::vector<float> land_potential{};
    std::vector<float> inland{};
    std::vector<float> ridge_strength{};
    std::vector<float> valley_strength{};
    std::vector<float> flow_accumulation{};
    std::vector<float> stream_power{};
    std::vector<float> flow_direction{};
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    float max_water_depth_m = 0.0F;
    float max_abs_shore_sdf_m = 0.0F;
    float max_flow_accumulation = 0.0F;
    float max_stream_power = 0.0F;

    [[nodiscard]] std::size_t sample_count() const;
    [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y) const;
};

[[nodiscard]] TerrainFieldData generate_terrain_fields(const TerrainConfig& config);

} // namespace cubey::projects::procedural_terrain
