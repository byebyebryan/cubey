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
    AridMesaCanyon = 0,
    TemperateMountainRivers = 1,
    DesertDunes = 2,
    AlpineGlacialValley = 3,
    MountainRidgesPeaks = 4,
};

inline constexpr std::array<TerrainLabSlicePreset, 5> kTerrainLabSlicePresets{
    TerrainLabSlicePreset::AridMesaCanyon,
    TerrainLabSlicePreset::TemperateMountainRivers,
    TerrainLabSlicePreset::DesertDunes,
    TerrainLabSlicePreset::AlpineGlacialValley,
    TerrainLabSlicePreset::MountainRidgesPeaks,
};

enum class TerrainLabCameraPreset : std::uint32_t {
    Orbit = 0,
    Profile = 1,
};

inline constexpr std::array<TerrainLabCameraPreset, 2> kTerrainLabCameraPresets{
    TerrainLabCameraPreset::Orbit,
    TerrainLabCameraPreset::Profile,
};

enum class TerrainLabNoiseSource : std::uint32_t {
    LegacyValue = 0,
    FastNoiseLite = 1,
};

inline constexpr std::array<TerrainLabNoiseSource, 2> kTerrainLabNoiseSources{
    TerrainLabNoiseSource::LegacyValue,
    TerrainLabNoiseSource::FastNoiseLite,
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
    RiverNetwork = 10,
    RiverWidth = 11,
    WaterPresence = 12,
    Wetness = 13,
    Deposition = 14,
    Material = 15,
    BiomeDensity = 16,
    CanopyHeight = 17,
    NoiseOff = 18,
    FeatureGraph = 19,
    DrainageRegions = 20,
    Channel = 21,
    Divide = 22,
    Driver = 23,
};

inline constexpr std::array<TerrainLabDebugView, 24> kTerrainLabDebugViews{
    TerrainLabDebugView::Final,
    TerrainLabDebugView::Height,
    TerrainLabDebugView::Structure,
    TerrainLabDebugView::Process,
    TerrainLabDebugView::Detail,
    TerrainLabDebugView::Slope,
    TerrainLabDebugView::Curvature,
    TerrainLabDebugView::FlowDirection,
    TerrainLabDebugView::FlowAccumulation,
    TerrainLabDebugView::StreamPower,
    TerrainLabDebugView::RiverNetwork,
    TerrainLabDebugView::RiverWidth,
    TerrainLabDebugView::WaterPresence,
    TerrainLabDebugView::Wetness,
    TerrainLabDebugView::Deposition,
    TerrainLabDebugView::Material,
    TerrainLabDebugView::BiomeDensity,
    TerrainLabDebugView::CanopyHeight,
    TerrainLabDebugView::NoiseOff,
    TerrainLabDebugView::FeatureGraph,
    TerrainLabDebugView::DrainageRegions,
    TerrainLabDebugView::Channel,
    TerrainLabDebugView::Divide,
    TerrainLabDebugView::Driver,
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
    TerrainLabSlicePreset slice_preset = TerrainLabSlicePreset::TemperateMountainRivers;
    TerrainLabCameraPreset camera_preset = TerrainLabCameraPreset::Orbit;
    TerrainLabNoiseSource noise_source = TerrainLabNoiseSource::LegacyValue;
    TerrainLabDebugView debug_view = TerrainLabDebugView::Final;

    friend bool operator==(const TerrainLabConfig&, const TerrainLabConfig&) = default;
};

[[nodiscard]] inline const char* terrain_lab_slice_preset_name(TerrainLabSlicePreset preset) {
    switch (preset) {
    case TerrainLabSlicePreset::AridMesaCanyon:
        return "arid-mesa-canyon";
    case TerrainLabSlicePreset::TemperateMountainRivers:
        return "temperate-mountain-rivers";
    case TerrainLabSlicePreset::DesertDunes:
        return "desert-dunes";
    case TerrainLabSlicePreset::AlpineGlacialValley:
        return "alpine-glacial-valley";
    case TerrainLabSlicePreset::MountainRidgesPeaks:
        return "mountain-ridges-peaks";
    }
    return "arid-mesa-canyon";
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
    case TerrainLabDebugView::RiverNetwork:
        return "river-network";
    case TerrainLabDebugView::RiverWidth:
        return "river-width";
    case TerrainLabDebugView::WaterPresence:
        return "water-presence";
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
    case TerrainLabDebugView::FeatureGraph:
        return "feature-graph";
    case TerrainLabDebugView::DrainageRegions:
        return "drainage-regions";
    case TerrainLabDebugView::Channel:
        return "channel";
    case TerrainLabDebugView::Divide:
        return "divide";
    case TerrainLabDebugView::Driver:
        return "driver";
    }
    return "final";
}

[[nodiscard]] inline const char* terrain_lab_camera_preset_name(TerrainLabCameraPreset preset) {
    switch (preset) {
    case TerrainLabCameraPreset::Orbit:
        return "orbit";
    case TerrainLabCameraPreset::Profile:
        return "profile";
    }
    return "orbit";
}

[[nodiscard]] inline const char* terrain_lab_noise_source_name(TerrainLabNoiseSource source) {
    switch (source) {
    case TerrainLabNoiseSource::LegacyValue:
        return "legacy-value";
    case TerrainLabNoiseSource::FastNoiseLite:
        return "fastnoise-lite";
    }
    return "legacy-value";
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

[[nodiscard]] inline TerrainLabSlicePreset
terrain_lab_slice_preset_from_name(std::string_view name) {
    if (name.empty()) {
        return TerrainLabSlicePreset::TemperateMountainRivers;
    }
    if (terrain_lab_name_matches(name, "temperate-mountain-watershed")) {
        return TerrainLabSlicePreset::TemperateMountainRivers;
    }
    for (const TerrainLabSlicePreset preset : kTerrainLabSlicePresets) {
        if (terrain_lab_name_matches(name, terrain_lab_slice_preset_name(preset))) {
            return preset;
        }
    }
    throw std::runtime_error("unknown terrain lab slice preset: " + std::string(name));
}

[[nodiscard]] inline TerrainLabDebugView terrain_lab_debug_view_from_name(std::string_view name) {
    if (name.empty()) {
        return TerrainLabDebugView::Final;
    }
    if (terrain_lab_name_matches(name, "watershed")) {
        return TerrainLabDebugView::DrainageRegions;
    }
    for (const TerrainLabDebugView view : kTerrainLabDebugViews) {
        if (terrain_lab_name_matches(name, terrain_lab_debug_view_name(view))) {
            return view;
        }
    }
    throw std::runtime_error("unknown terrain lab debug view: " + std::string(name));
}

[[nodiscard]] inline TerrainLabCameraPreset
terrain_lab_camera_preset_from_name(std::string_view name) {
    if (name.empty()) {
        return TerrainLabCameraPreset::Orbit;
    }
    for (const TerrainLabCameraPreset preset : kTerrainLabCameraPresets) {
        if (terrain_lab_name_matches(name, terrain_lab_camera_preset_name(preset))) {
            return preset;
        }
    }
    throw std::runtime_error("unknown terrain lab camera preset: " + std::string(name));
}

[[nodiscard]] inline TerrainLabNoiseSource terrain_lab_noise_source_from_name(
    std::string_view name) {
    if (name.empty()) {
        return TerrainLabNoiseSource::LegacyValue;
    }
    for (const TerrainLabNoiseSource source : kTerrainLabNoiseSources) {
        if (terrain_lab_name_matches(name, terrain_lab_noise_source_name(source))) {
            return source;
        }
    }
    throw std::runtime_error("unknown terrain lab noise source: " + std::string(name));
}

[[nodiscard]] inline TerrainLabDebugView next_terrain_lab_debug_view(TerrainLabDebugView view) {
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

[[nodiscard]] inline TerrainLabConfig terrain_lab_config_from_run_config(const RunConfig& config) {
    TerrainLabConfig terrain;
    if (config.grid.width != 0U) {
        terrain.grid_width = config.grid.width;
    }
    if (config.grid.height != 0U) {
        terrain.grid_height = config.grid.height;
    }
    terrain.debug_view = terrain_lab_debug_view_from_name(config.debug_view);
    terrain.slice_preset = terrain_lab_slice_preset_from_name(config.terrain_lab.slice_preset);
    terrain.camera_preset = terrain_lab_camera_preset_from_name(config.terrain_lab.camera_preset);
    terrain.noise_source = terrain_lab_noise_source_from_name(config.terrain_lab.noise_source);
    validate_terrain_lab_config(terrain);
    return terrain;
}

} // namespace cubey::projects::terrain_lab
