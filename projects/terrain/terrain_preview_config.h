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
};

inline constexpr float kTerrainPreviewDefaultVerticalScale = 1.0F;
inline constexpr std::string_view kTerrainPreviewDefaultCameraPreset = "oblique";
inline constexpr std::string_view kTerrainPreviewDefaultRecipe =
    kTerrainRecipeTemperateMountainRangeStress;

struct TerrainPreviewConfig {
    TerrainRegionConfig region{};
    TerrainPreviewCameraPreset camera_preset = TerrainPreviewCameraPreset::Oblique;
    float vertical_scale = kTerrainPreviewDefaultVerticalScale;
};

[[nodiscard]] std::string_view terrain_preview_camera_preset_name(
    TerrainPreviewCameraPreset preset);
[[nodiscard]] TerrainPreviewCameraPreset terrain_preview_camera_preset_from_name(
    std::string_view name);
[[nodiscard]] TerrainPreviewConfig terrain_preview_config_from_run_config(
    const cubey::RunConfig& config);

} // namespace cubey::projects::terrain
