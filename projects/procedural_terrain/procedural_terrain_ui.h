#pragma once

#include "procedural_terrain_config.h"

#include <cubey/host/frame_stats.h>

#include <cstddef>
#include <cstdint>
#include <optional>

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
    float sand_coverage = 0.0F;
    float rock_coverage = 0.0F;
    float vegetation_coverage = 0.0F;
    float sediment_coverage = 0.0F;
    std::uint32_t terrain_vertices = 0;
    std::uint32_t terrain_triangles = 0;
    std::uint32_t water_vertices = 0;
    std::uint32_t water_triangles = 0;
    double last_rebuild_ms = 0.0;
    std::uint64_t rebuild_count = 0;
};

struct TerrainUiContext {
    TerrainConfig& active_config;
    TerrainConfig& edit_config;
    const TerrainDiagnostics& diagnostics;
    std::optional<cubey::host::FrameStatsSnapshot>& latest_frame_stats;
    bool& water_visible;
    bool& rebuild_requested;
    bool& reset_camera_requested;
    double latest_fps = 0.0;
    double latest_frame_ms = 0.0;
};

void draw_terrain_ui(TerrainUiContext ui);

} // namespace cubey::projects::procedural_terrain
