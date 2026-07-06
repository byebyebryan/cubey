#pragma once

#include "terrain_config.h"

#include <cubey/core/run_config.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainPreviewCameraPreset : std::uint8_t {
    Oblique,
    Profile,
    Top,
    Surface,
    SurfaceLow,
};

enum class TerrainPreviewColorMode : std::uint8_t {
    Material,
    Height,
    River,
    Channel,
};

enum class TerrainPreviewSurface : std::uint8_t {
    Height,
    PostErosion,
    PreProcess,
};

enum class TerrainPreviewRuntimeMode : std::uint8_t {
    CpuProduct,
    TerrainEngineReference,
};

inline constexpr float kTerrainPreviewDefaultVerticalScale = 1.0F;
inline constexpr std::string_view kTerrainPreviewDefaultCameraPreset = "oblique";
inline constexpr std::string_view kTerrainPreviewDefaultColorMode = "material";
inline constexpr std::string_view kTerrainPreviewDefaultSurface = "height";
inline constexpr std::string_view kTerrainPreviewDefaultRuntimeMode = "cpu-product";
inline constexpr std::string_view kTerrainPreviewDefaultRecipe =
    kTerrainRecipeTemperateMountainRangeStress;

struct TerrainPreviewConfig {
    TerrainRegionConfig region{};
    TerrainPreviewCameraPreset camera_preset = TerrainPreviewCameraPreset::Oblique;
    TerrainPreviewColorMode color_mode = TerrainPreviewColorMode::Material;
    TerrainPreviewSurface surface = TerrainPreviewSurface::Height;
    TerrainPreviewRuntimeMode runtime_mode = TerrainPreviewRuntimeMode::CpuProduct;
    float vertical_scale = kTerrainPreviewDefaultVerticalScale;
    bool water_surface = false;
};

[[nodiscard]] std::string_view
terrain_preview_camera_preset_name(TerrainPreviewCameraPreset preset);
[[nodiscard]] TerrainPreviewCameraPreset
terrain_preview_camera_preset_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_preview_color_mode_name(TerrainPreviewColorMode mode);
[[nodiscard]] TerrainPreviewColorMode terrain_preview_color_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_preview_surface_name(TerrainPreviewSurface surface);
[[nodiscard]] TerrainPreviewSurface terrain_preview_surface_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_preview_surface_field_name(TerrainPreviewSurface surface);
[[nodiscard]] std::string_view
terrain_preview_runtime_mode_name(TerrainPreviewRuntimeMode runtime_mode);
[[nodiscard]] TerrainPreviewRuntimeMode
terrain_preview_runtime_mode_from_name(std::string_view name);
[[nodiscard]] TerrainPreviewConfig
terrain_preview_config_from_run_config(const cubey::RunConfig& config);

} // namespace cubey::projects::terrain
