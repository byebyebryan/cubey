#pragma once

#include <cubey/core/run_config.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::terrain_lab {

enum class TerrainLabSlicePreset : std::uint32_t {
    TemperateMountainWatershed = 0,
};

inline constexpr std::array<TerrainLabSlicePreset, 1> kTerrainLabSlicePresets{
    TerrainLabSlicePreset::TemperateMountainWatershed,
};

enum class TerrainLabDebugView : std::uint32_t {
    Final = 0,
    Height = 1,
    Structure = 2,
    Process = 3,
    Detail = 4,
    Slope = 5,
    Curvature = 6,
    FlowDirection = 7,
    FlowAccumulation = 8,
    StreamPower = 9,
    Wetness = 10,
    Deposition = 11,
    Material = 12,
    BiomeDensity = 13,
    CanopyHeight = 14,
    NoiseOff = 15,
};

inline constexpr std::array<TerrainLabDebugView, 16> kTerrainLabDebugViews{
    TerrainLabDebugView::Final,       TerrainLabDebugView::Height,
    TerrainLabDebugView::Structure,   TerrainLabDebugView::Process,
    TerrainLabDebugView::Detail,      TerrainLabDebugView::Slope,
    TerrainLabDebugView::Curvature,   TerrainLabDebugView::FlowDirection,
    TerrainLabDebugView::FlowAccumulation,
    TerrainLabDebugView::StreamPower, TerrainLabDebugView::Wetness,
    TerrainLabDebugView::Deposition,  TerrainLabDebugView::Material,
    TerrainLabDebugView::BiomeDensity,
    TerrainLabDebugView::CanopyHeight,
    TerrainLabDebugView::NoiseOff,
};

inline constexpr std::uint32_t kTerrainLabDefaultGridWidth = 257U;
inline constexpr std::uint32_t kTerrainLabDefaultGridHeight = 257U;
inline constexpr std::uint32_t kTerrainLabMinGridExtent = 17U;
inline constexpr std::uint32_t kTerrainLabMaxGridExtent = 2049U;
inline constexpr std::uint64_t kTerrainLabDefaultSeed = 0x7e22'2026'0613ULL;
inline constexpr float kTerrainLabDefaultCellSizeMeters = 32.0F;
inline constexpr float kTerrainLabDefaultElevationScaleMeters = 900.0F;
inline constexpr float kTerrainLabDefaultStructureStrength = 1.0F;
inline constexpr float kTerrainLabDefaultProcessStrength = 1.0F;
inline constexpr float kTerrainLabDefaultDetailStrength = 1.0F;

struct TerrainLabConfig {
    std::uint32_t grid_width = kTerrainLabDefaultGridWidth;
    std::uint32_t grid_height = kTerrainLabDefaultGridHeight;
    std::uint64_t seed = kTerrainLabDefaultSeed;
    float cell_size_m = kTerrainLabDefaultCellSizeMeters;
    float elevation_scale_m = kTerrainLabDefaultElevationScaleMeters;
    float structure_strength = kTerrainLabDefaultStructureStrength;
    float process_strength = kTerrainLabDefaultProcessStrength;
    float detail_strength = kTerrainLabDefaultDetailStrength;
    TerrainLabSlicePreset slice_preset = TerrainLabSlicePreset::TemperateMountainWatershed;
    TerrainLabDebugView debug_view = TerrainLabDebugView::Final;

    friend bool operator==(const TerrainLabConfig&, const TerrainLabConfig&) = default;
};

[[nodiscard]] inline const char* terrain_lab_slice_preset_name(
    TerrainLabSlicePreset preset) {
    switch (preset) {
    case TerrainLabSlicePreset::TemperateMountainWatershed:
        return "temperate-mountain-watershed";
    }
    return "temperate-mountain-watershed";
}

[[nodiscard]] inline const char* terrain_lab_debug_view_name(TerrainLabDebugView view) {
    switch (view) {
    case TerrainLabDebugView::Final:
        return "final";
    case TerrainLabDebugView::Height:
        return "height";
    case TerrainLabDebugView::Structure:
        return "structure";
    case TerrainLabDebugView::Process:
        return "process";
    case TerrainLabDebugView::Detail:
        return "detail";
    case TerrainLabDebugView::Slope:
        return "slope";
    case TerrainLabDebugView::Curvature:
        return "curvature";
    case TerrainLabDebugView::FlowDirection:
        return "flow-direction";
    case TerrainLabDebugView::FlowAccumulation:
        return "flow-accumulation";
    case TerrainLabDebugView::StreamPower:
        return "stream-power";
    case TerrainLabDebugView::Wetness:
        return "wetness";
    case TerrainLabDebugView::Deposition:
        return "deposition";
    case TerrainLabDebugView::Material:
        return "material";
    case TerrainLabDebugView::BiomeDensity:
        return "biome-density";
    case TerrainLabDebugView::CanopyHeight:
        return "canopy-height";
    case TerrainLabDebugView::NoiseOff:
        return "noise-off";
    }
    return "final";
}

[[nodiscard]] inline bool terrain_lab_name_matches(std::string_view value,
                                                   std::string_view canonical) {
    if (value.size() != canonical.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char lhs = value[index] == '_' ? '-' : value[index];
        const char rhs = canonical[index] == '_' ? '-' : canonical[index];
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline TerrainLabSlicePreset terrain_lab_slice_preset_from_name(
    std::string_view name) {
    if (name.empty()) {
        return TerrainLabSlicePreset::TemperateMountainWatershed;
    }
    for (const TerrainLabSlicePreset preset : kTerrainLabSlicePresets) {
        if (terrain_lab_name_matches(name, terrain_lab_slice_preset_name(preset))) {
            return preset;
        }
    }
    throw std::runtime_error("unknown terrain lab slice preset: " + std::string(name));
}

[[nodiscard]] inline TerrainLabDebugView terrain_lab_debug_view_from_name(
    std::string_view name) {
    if (name.empty()) {
        return TerrainLabDebugView::Final;
    }
    for (const TerrainLabDebugView view : kTerrainLabDebugViews) {
        if (terrain_lab_name_matches(name, terrain_lab_debug_view_name(view))) {
            return view;
        }
    }
    throw std::runtime_error("unknown terrain lab debug view: " + std::string(name));
}

[[nodiscard]] inline TerrainLabDebugView next_terrain_lab_debug_view(
    TerrainLabDebugView view) {
    for (std::size_t index = 0; index < kTerrainLabDebugViews.size(); ++index) {
        if (kTerrainLabDebugViews[index] == view) {
            return kTerrainLabDebugViews[(index + 1U) % kTerrainLabDebugViews.size()];
        }
    }
    return TerrainLabDebugView::Final;
}

inline void validate_terrain_lab_config(const TerrainLabConfig& config) {
    if (config.grid_width < kTerrainLabMinGridExtent ||
        config.grid_width > kTerrainLabMaxGridExtent ||
        config.grid_height < kTerrainLabMinGridExtent ||
        config.grid_height > kTerrainLabMaxGridExtent) {
        throw std::runtime_error("terrain lab grid dimensions must be in [17, 2049]");
    }
    if (!std::isfinite(config.cell_size_m) || config.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain lab cell size must be positive");
    }
    if (!std::isfinite(config.elevation_scale_m) || config.elevation_scale_m <= 0.0F ||
        config.elevation_scale_m > 8000.0F) {
        throw std::runtime_error("terrain lab elevation scale must be in (0, 8000]");
    }
    if (!std::isfinite(config.structure_strength) || config.structure_strength < 0.0F ||
        config.structure_strength > 4.0F) {
        throw std::runtime_error("terrain lab structure strength must be in [0, 4]");
    }
    if (!std::isfinite(config.process_strength) || config.process_strength < 0.0F ||
        config.process_strength > 4.0F) {
        throw std::runtime_error("terrain lab process strength must be in [0, 4]");
    }
    if (!std::isfinite(config.detail_strength) || config.detail_strength < 0.0F ||
        config.detail_strength > 4.0F) {
        throw std::runtime_error("terrain lab detail strength must be in [0, 4]");
    }
}

[[nodiscard]] inline TerrainLabConfig terrain_lab_config_from_run_config(
    const RunConfig& config) {
    TerrainLabConfig terrain;
    if (config.grid.width != 0U) {
        terrain.grid_width = config.grid.width;
    }
    if (config.grid.height != 0U) {
        terrain.grid_height = config.grid.height;
    }
    terrain.debug_view = terrain_lab_debug_view_from_name(config.debug_view);
    validate_terrain_lab_config(terrain);
    return terrain;
}

} // namespace cubey::projects::terrain_lab
