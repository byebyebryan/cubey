#pragma once

#include "terrain_product.h"

#include <filesystem>
#include <span>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainDebugView {
    Final,
    Height,
    Slope,
    MountainSupport,
    RidgeSupport,
    PeakSupport,
    MountainUplift,
    RidgeUplift,
    PeakUplift,
    DrainagePotential,
    RoutingFillDelta,
    FlowDirection,
    FlowAccumulation,
    StreamOrder,
    RiverMask,
    RiverTrunk,
    Tributaries,
    RiverGraphPlan,
    RiverGraphDischarge,
    SinkMask,
    ChannelWidth,
    Wetness,
    Deposition,
    Material,
    Vegetation,
};

[[nodiscard]] std::string_view terrain_debug_view_name(TerrainDebugView view);
[[nodiscard]] std::span<const TerrainDebugView> terrain_debug_review_views();
[[nodiscard]] TerrainDebugView terrain_debug_view_from_name(std::string_view name);
void write_terrain_debug_png(const TerrainRegionProduct& product, TerrainDebugView view,
                             const std::filesystem::path& output_path);
void write_terrain_debug_review_pngs(const TerrainRegionProduct& product,
                                     const std::filesystem::path& output_dir);

} // namespace cubey::projects::terrain
