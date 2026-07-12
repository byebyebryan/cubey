#pragma once

#include "terrain_source.h"

#include <cubey/core/run_config.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainCameraPreset : std::uint8_t {
    Oblique,
    Profile,
    Top,
    Surface,
    SurfaceLow,
    Ground,
    Backdrop,
};

enum class TerrainDebugView : std::uint8_t {
    Surface,
    Height,
    BaseHeight,
    Slope,
    Weathering,
    Lod,
    Clay,
    Shadow,
    AerialTransmittance,
    VegetationCoverage,
    Normal,
};

enum class TerrainPresentationMode : std::uint8_t {
    Standard,
    Backdrop,
};

struct TerrainRuntimeConfig {
    TerrainSourceConfig source{};
    TerrainCameraPreset camera = TerrainCameraPreset::Oblique;
    TerrainDebugView debug_view = TerrainDebugView::Surface;
    TerrainPresentationMode presentation = TerrainPresentationMode::Standard;
    float near_cell_size_m = 2.0F;
    float vertical_scale = 1.0F;
    std::uint32_t lod_levels = 8U;
    std::uint32_t cells_per_axis = 128U;
};

[[nodiscard]] std::string_view terrain_camera_preset_name(TerrainCameraPreset preset);
[[nodiscard]] TerrainCameraPreset terrain_camera_preset_from_name(std::string_view name);
[[nodiscard]] bool terrain_camera_is_surface(TerrainCameraPreset preset) noexcept;
[[nodiscard]] bool terrain_camera_advances_headless(TerrainCameraPreset preset) noexcept;
[[nodiscard]] float terrain_camera_clearance_m(TerrainCameraPreset preset);
[[nodiscard]] float terrain_camera_traversal_speed_mps(TerrainCameraPreset preset) noexcept;
[[nodiscard]] float terrain_camera_fovy_radians(TerrainCameraPreset preset) noexcept;
[[nodiscard]] std::string_view terrain_debug_view_name(TerrainDebugView view);
[[nodiscard]] TerrainDebugView terrain_debug_view_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_presentation_mode_name(TerrainPresentationMode mode);
[[nodiscard]] TerrainPresentationMode terrain_presentation_mode_from_name(std::string_view name);
void validate_terrain_runtime_config(const TerrainRuntimeConfig& config);
[[nodiscard]] TerrainRuntimeConfig terrain_runtime_config_from_run_config(const RunConfig& config);

} // namespace cubey::projects::terrain
