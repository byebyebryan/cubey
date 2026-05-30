#pragma once

#include <cubey/core/run_config.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::procedural_terrain {

enum class TerrainDebugView : std::uint32_t {
    Final = 0,
    Height = 1,
    WaterDepth = 2,
    Shoreline = 3,
    Material = 4,
    Slope = 5,
};

inline constexpr std::array<TerrainDebugView, 6> kTerrainDebugViews{
    TerrainDebugView::Final,     TerrainDebugView::Height,   TerrainDebugView::WaterDepth,
    TerrainDebugView::Shoreline, TerrainDebugView::Material, TerrainDebugView::Slope,
};

inline constexpr std::uint32_t kTerrainDefaultGridWidth = 129U;
inline constexpr std::uint32_t kTerrainDefaultGridHeight = 129U;
inline constexpr std::uint32_t kTerrainMinGridExtent = 17U;
inline constexpr std::uint32_t kTerrainMaxGridExtent = 513U;
inline constexpr std::uint64_t kTerrainDefaultSeed = 0x54e2'2026'0529ULL;
inline constexpr float kTerrainDefaultCellSizeMeters = 4.0F;
inline constexpr float kTerrainDefaultSeaLevelMeters = 0.0F;

struct TerrainConfig {
    std::uint32_t grid_width = kTerrainDefaultGridWidth;
    std::uint32_t grid_height = kTerrainDefaultGridHeight;
    std::uint64_t seed = kTerrainDefaultSeed;
    float cell_size_m = kTerrainDefaultCellSizeMeters;
    float sea_level_m = kTerrainDefaultSeaLevelMeters;
    TerrainDebugView debug_view = TerrainDebugView::Final;

    friend bool operator==(const TerrainConfig&, const TerrainConfig&) = default;
};

[[nodiscard]] inline const char* terrain_debug_view_name(TerrainDebugView view) {
    switch (view) {
    case TerrainDebugView::Final:
        return "final";
    case TerrainDebugView::Height:
        return "height";
    case TerrainDebugView::WaterDepth:
        return "water_depth";
    case TerrainDebugView::Shoreline:
        return "shoreline";
    case TerrainDebugView::Material:
        return "material";
    case TerrainDebugView::Slope:
        return "slope";
    }
    return "final";
}

[[nodiscard]] inline TerrainDebugView terrain_debug_view_from_name(std::string_view name) {
    if (name.empty()) {
        return TerrainDebugView::Final;
    }
    for (const TerrainDebugView view : kTerrainDebugViews) {
        if (name == terrain_debug_view_name(view)) {
            return view;
        }
    }
    throw std::runtime_error("unknown terrain debug view: " + std::string(name));
}

[[nodiscard]] inline TerrainDebugView next_terrain_debug_view(TerrainDebugView view) {
    for (std::size_t index = 0; index < kTerrainDebugViews.size(); ++index) {
        if (kTerrainDebugViews[index] == view) {
            return kTerrainDebugViews[(index + 1U) % kTerrainDebugViews.size()];
        }
    }
    return TerrainDebugView::Final;
}

inline void validate_terrain_config(const TerrainConfig& config) {
    if (config.grid_width < kTerrainMinGridExtent || config.grid_width > kTerrainMaxGridExtent ||
        config.grid_height < kTerrainMinGridExtent || config.grid_height > kTerrainMaxGridExtent) {
        throw std::runtime_error("terrain grid dimensions must be in [17, 513]");
    }
    if (config.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain cell size must be positive");
    }
}

[[nodiscard]] inline TerrainConfig terrain_config_from_run_config(const RunConfig& config) {
    TerrainConfig terrain;
    if (config.grid.width != 0U) {
        terrain.grid_width = config.grid.width;
    }
    if (config.grid.height != 0U) {
        terrain.grid_height = config.grid.height;
    }
    terrain.debug_view = terrain_debug_view_from_name(config.debug_view);
    validate_terrain_config(terrain);
    return terrain;
}

} // namespace cubey::projects::procedural_terrain
