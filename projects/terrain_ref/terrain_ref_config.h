#pragma once

#include "terrain_engine_reference.h"

#include <cubey/core/run_config.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain_ref {

enum class TerrainRefCameraPreset : std::uint8_t {
    Oblique,
    Profile,
    Top,
    Surface,
    SurfaceLow,
};

enum class TerrainRefRecipe : std::uint8_t {
    TerrainEngine,
    ShadertoyMountain,
};

inline constexpr std::string_view kTerrainRefRecipeShadertoyMountain = "shadertoy-mountain";
inline constexpr std::string_view kTerrainRefDefaultCameraPreset = "oblique";
inline constexpr float kTerrainRefDefaultCellSizeM = 32.0F;
inline constexpr std::uint32_t kTerrainRefDefaultGridSize = 513U;
inline constexpr float kTerrainRefDefaultVerticalScale = 1.0F;

struct TerrainRefConfig {
    TerrainRefRecipe recipe = TerrainRefRecipe::TerrainEngine;
    std::uint64_t seed = kTerrainRefDefaultSeed;
    std::uint32_t grid_width = kTerrainRefDefaultGridSize;
    std::uint32_t grid_height = kTerrainRefDefaultGridSize;
    float cell_size_m = kTerrainRefDefaultCellSizeM;
    float vertical_scale = kTerrainRefDefaultVerticalScale;
    TerrainRefCameraPreset camera_preset = TerrainRefCameraPreset::Oblique;
    bool water_surface = true;
};

[[nodiscard]] std::string_view terrain_ref_recipe_name(TerrainRefRecipe recipe);
[[nodiscard]] TerrainRefRecipe terrain_ref_recipe_from_name(std::string_view name);
[[nodiscard]] std::string_view terrain_ref_camera_preset_name(TerrainRefCameraPreset preset);
[[nodiscard]] TerrainRefCameraPreset terrain_ref_camera_preset_from_name(std::string_view name);
[[nodiscard]] TerrainRefConfig terrain_ref_config_from_run_config(const cubey::RunConfig& config);
[[nodiscard]] float terrain_ref_extent_m(const TerrainRefConfig& config);

} // namespace cubey::projects::terrain_ref
