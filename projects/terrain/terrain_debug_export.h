#pragma once

#include "terrain_product.h"

#include <filesystem>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainDebugView {
    Final,
    Height,
    Slope,
    RidgeUplift,
    FlowAccumulation,
    StreamOrder,
    RiverMask,
    Wetness,
    Deposition,
    Material,
    Vegetation,
};

[[nodiscard]] std::string_view terrain_debug_view_name(TerrainDebugView view);
[[nodiscard]] TerrainDebugView terrain_debug_view_from_name(std::string_view name);
void write_terrain_debug_png(const TerrainRegionProduct& product, TerrainDebugView view,
                             const std::filesystem::path& output_path);

} // namespace cubey::projects::terrain
