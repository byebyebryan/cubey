#pragma once

#include <cubey/core/run_config.h>

#include <array>
#include <cmath>
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

inline constexpr std::uint32_t kTerrainDefaultGridWidth = 385U;
inline constexpr std::uint32_t kTerrainDefaultGridHeight = 385U;
inline constexpr std::uint32_t kTerrainMinGridExtent = 17U;
inline constexpr std::uint32_t kTerrainMaxGridExtent = 513U;
inline constexpr std::uint64_t kTerrainDefaultSeed = 0x54e2'2026'0529ULL;
inline constexpr float kTerrainDefaultCellSizeMeters = 4.0F;
inline constexpr float kTerrainDefaultSeaLevelMeters = 0.0F;
inline constexpr float kTerrainDefaultLandExtent = 0.70F;
inline constexpr float kTerrainDefaultCoastNoiseStrength = 0.20F;
inline constexpr float kTerrainDefaultReliefScale = 1.0F;
inline constexpr float kTerrainDefaultRidgeScale = 1.0F;

struct TerrainConfig {
    std::uint32_t grid_width = kTerrainDefaultGridWidth;
    std::uint32_t grid_height = kTerrainDefaultGridHeight;
    std::uint64_t seed = kTerrainDefaultSeed;
    float cell_size_m = kTerrainDefaultCellSizeMeters;
    float sea_level_m = kTerrainDefaultSeaLevelMeters;
    float land_extent = kTerrainDefaultLandExtent;
    float coast_noise_strength = kTerrainDefaultCoastNoiseStrength;
    float relief_scale = kTerrainDefaultReliefScale;
    float ridge_scale = kTerrainDefaultRidgeScale;
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
    if (!std::isfinite(config.cell_size_m) || config.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain cell size must be positive");
    }
    if (!std::isfinite(config.sea_level_m)) {
        throw std::runtime_error("terrain sea level must be finite");
    }
    if (!std::isfinite(config.land_extent) || config.land_extent < 0.35F ||
        config.land_extent > 0.90F) {
        throw std::runtime_error("terrain land extent must be in [0.35, 0.90]");
    }
    if (!std::isfinite(config.coast_noise_strength) || config.coast_noise_strength < 0.0F ||
        config.coast_noise_strength > 0.50F) {
        throw std::runtime_error("terrain coast noise strength must be in [0.0, 0.50]");
    }
    if (!std::isfinite(config.relief_scale) || config.relief_scale < 0.20F ||
        config.relief_scale > 2.0F) {
        throw std::runtime_error("terrain relief scale must be in [0.20, 2.0]");
    }
    if (!std::isfinite(config.ridge_scale) || config.ridge_scale < 0.0F ||
        config.ridge_scale > 2.0F) {
        throw std::runtime_error("terrain ridge scale must be in [0.0, 2.0]");
    }
}

[[nodiscard]] inline bool terrain_rebuild_config_equal(const TerrainConfig& lhs,
                                                       const TerrainConfig& rhs) {
    return lhs.grid_width == rhs.grid_width && lhs.grid_height == rhs.grid_height &&
           lhs.seed == rhs.seed && lhs.cell_size_m == rhs.cell_size_m &&
           lhs.sea_level_m == rhs.sea_level_m && lhs.land_extent == rhs.land_extent &&
           lhs.coast_noise_strength == rhs.coast_noise_strength &&
           lhs.relief_scale == rhs.relief_scale && lhs.ridge_scale == rhs.ridge_scale;
}

[[nodiscard]] inline TerrainConfig terrain_config_from_run_config(const RunConfig& config) {
    TerrainConfig terrain;
    if (config.grid.width != 0U) {
        terrain.grid_width = config.grid.width;
    }
    if (config.grid.height != 0U) {
        terrain.grid_height = config.grid.height;
    }
    if (config.terrain.seed_set) {
        terrain.seed = config.terrain.seed;
    }
    if (run_config_float_is_set(config.terrain.cell_size)) {
        terrain.cell_size_m = config.terrain.cell_size;
    }
    if (run_config_float_is_set(config.terrain.sea_level)) {
        terrain.sea_level_m = config.terrain.sea_level;
    }
    if (run_config_float_is_set(config.terrain.land_extent)) {
        terrain.land_extent = config.terrain.land_extent;
    }
    if (run_config_float_is_set(config.terrain.coast_noise)) {
        terrain.coast_noise_strength = config.terrain.coast_noise;
    }
    if (run_config_float_is_set(config.terrain.relief)) {
        terrain.relief_scale = config.terrain.relief;
    }
    if (run_config_float_is_set(config.terrain.ridges)) {
        terrain.ridge_scale = config.terrain.ridges;
    }
    terrain.debug_view = terrain_debug_view_from_name(config.debug_view);
    validate_terrain_config(terrain);
    return terrain;
}

} // namespace cubey::projects::procedural_terrain
