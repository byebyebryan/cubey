#pragma once

#include "terrain_lab_config.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cubey::projects::terrain_lab {

struct TerrainLabGridDesc {
    std::uint32_t version = 1;
    std::uint64_t seed = 0;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    float cell_size_m = 1.0F;
    float origin_x_m = 0.0F;
    float origin_z_m = 0.0F;
};

struct TerrainLabMaterialMask {
    float rock = 0.0F;
    float soil = 0.0F;
    float scree = 0.0F;
    float meadow = 0.0F;
    float forest = 0.0F;
    float snow = 0.0F;
    float sand = 0.0F;
};

struct TerrainLabFieldSummary {
    std::size_t sample_count = 0;
    std::size_t channel_sample_count = 0;
    std::size_t non_channel_sample_count = 0;
    std::size_t divide_sample_count = 0;
    std::uint32_t watershed_count = 0;
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    float height_span_m = 0.0F;
    float mean_height_m = 0.0F;
    float mean_slope = 0.0F;
    float max_flow_accumulation = 0.0F;
    float mean_channel_height_m = 0.0F;
    float mean_divide_height_m = 0.0F;
    float divide_channel_height_gap_m = 0.0F;
    float mean_channel_flow_accumulation = 0.0F;
    float mean_non_channel_flow_accumulation = 0.0F;
    float mean_wetness = 0.0F;
    float mean_tree_density = 0.0F;
    float mean_material_entropy = 0.0F;
    float mean_edge_step_m = 0.0F;
    float mean_divide_influence = 0.0F;
    float mean_channel_influence = 0.0F;
    float max_channel_distance_m = 0.0F;
};

struct TerrainLabFieldData {
    TerrainLabGridDesc desc{};
    std::vector<float> height_m{};
    std::vector<float> structure_height_m{};
    std::vector<float> process_delta_m{};
    std::vector<float> detail_height_m{};
    std::vector<float> slope{};
    std::vector<float> curvature{};
    std::vector<std::uint8_t> flow_direction{};
    std::vector<float> flow_accumulation{};
    std::vector<float> stream_power{};
    std::vector<float> wetness{};
    std::vector<float> deposition{};
    std::vector<TerrainLabMaterialMask> material_masks{};
    std::vector<float> grass_density{};
    std::vector<float> shrub_density{};
    std::vector<float> tree_density{};
    std::vector<float> canopy_height_m{};
    std::vector<float> ridge_influence{};
    std::vector<float> valley_influence{};
    std::vector<float> basin_influence{};
    std::vector<std::uint32_t> watershed_id{};
    std::vector<float> divide_influence{};
    std::vector<float> channel_influence{};
    std::vector<float> channel_distance_m{};
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    float max_slope = 0.0F;
    float max_abs_curvature = 0.0F;
    float max_flow_accumulation = 0.0F;
    float max_stream_power = 0.0F;
    float max_wetness = 0.0F;
    float max_deposition = 0.0F;
    float max_channel_distance_m = 0.0F;
    std::uint32_t watershed_count = 0;

    [[nodiscard]] std::size_t sample_count() const;
    [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y) const;
};

[[nodiscard]] std::size_t terrain_lab_sample_count(const TerrainLabGridDesc& desc);
[[nodiscard]] float terrain_lab_grid_sample_x_m(const TerrainLabGridDesc& desc,
                                                std::uint32_t x);
[[nodiscard]] float terrain_lab_grid_sample_z_m(const TerrainLabGridDesc& desc,
                                                std::uint32_t y);
void validate_terrain_lab_fields(const TerrainLabFieldData& fields);
[[nodiscard]] TerrainLabFieldSummary summarize_terrain_lab_fields(
    const TerrainLabFieldData& fields);
[[nodiscard]] TerrainLabFieldData generate_terrain_lab_fields(const TerrainLabConfig& config);

} // namespace cubey::projects::terrain_lab
