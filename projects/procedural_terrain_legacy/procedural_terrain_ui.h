#pragma once

#include "procedural_terrain_config.h"

#include <cubey/host/performance_ui.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace cubey::projects::procedural_terrain {

struct TerrainDiagnostics {
    std::size_t sample_count = 0;
    std::size_t land_samples = 0;
    std::size_t water_samples = 0;
    std::size_t shoreline_samples = 0;
    float min_height_m = 0.0F;
    float max_height_m = 0.0F;
    float max_water_depth_m = 0.0F;
    float max_abs_shore_sdf_m = 0.0F;
    float average_slope = 0.0F;
    float max_slope = 0.0F;
    float ridge_coverage = 0.0F;
    float valley_coverage = 0.0F;
    float max_abs_macro_height_m = 0.0F;
    float max_abs_base_noise_m = 0.0F;
    float max_abs_detail_noise_m = 0.0F;
    float max_abs_feature_height_m = 0.0F;
    float max_abs_relax_delta_m = 0.0F;
    float max_flow_accumulation = 0.0F;
    float max_stream_power = 0.0F;
    float sand_coverage = 0.0F;
    float rock_coverage = 0.0F;
    float vegetation_coverage = 0.0F;
    float sediment_coverage = 0.0F;
    std::uint32_t terrain_vertices = 0;
    std::uint32_t terrain_triangles = 0;
    std::uint32_t final_land_vertices = 0;
    std::uint32_t final_land_triangles = 0;
    std::uint32_t water_vertices = 0;
    std::uint32_t water_triangles = 0;
    std::uint32_t clipmap_lod_levels = 0;
    std::uint32_t clipmap_patch_count = 0;
    float clipmap_outer_half_extent_m = 0.0F;
    float clipmap_near_cell_size_m = 0.0F;
    double last_rebuild_ms = 0.0;
    std::uint64_t rebuild_count = 0;
};

struct TerrainUiContext {
    TerrainConfig& active_config;
    TerrainConfig& edit_config;
    const TerrainDiagnostics& diagnostics;
    cubey::host::PerformanceUiContext performance;
    std::string& rebuild_error;
    bool& water_visible;
    bool& rebuild_requested;
    bool& discard_edits_requested;
    bool& reset_camera_requested;
};

void draw_terrain_ui(TerrainUiContext ui);

} // namespace cubey::projects::procedural_terrain
