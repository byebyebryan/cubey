#include "terrain_ref_config.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain_ref {

std::string_view terrain_ref_camera_preset_name(TerrainRefCameraPreset preset) {
    switch (preset) {
    case TerrainRefCameraPreset::Oblique:
        return "oblique";
    case TerrainRefCameraPreset::Profile:
        return "profile";
    case TerrainRefCameraPreset::Top:
        return "top";
    case TerrainRefCameraPreset::Surface:
        return "surface";
    case TerrainRefCameraPreset::SurfaceLow:
        return "surface-low";
    }
    return "oblique";
}

TerrainRefCameraPreset terrain_ref_camera_preset_from_name(std::string_view name) {
    if (name == "oblique") {
        return TerrainRefCameraPreset::Oblique;
    }
    if (name == "profile") {
        return TerrainRefCameraPreset::Profile;
    }
    if (name == "top") {
        return TerrainRefCameraPreset::Top;
    }
    if (name == "surface") {
        return TerrainRefCameraPreset::Surface;
    }
    if (name == "surface-low" || name == "surface_low") {
        return TerrainRefCameraPreset::SurfaceLow;
    }
    throw std::runtime_error(
        "terrain_ref camera preset must be oblique, profile, top, surface, or surface-low");
}

TerrainRefConfig terrain_ref_config_from_run_config(const cubey::RunConfig& config) {
    if (!is_terrain_engine_reference_recipe(config.terrain.recipe)) {
        throw std::runtime_error("terrain_ref currently accepts only terrain-engine-ref recipe");
    }

    TerrainRefConfig result;
    result.grid_width = config.grid.width == 0U ? kTerrainRefDefaultGridSize : config.grid.width;
    result.grid_height = config.grid.height == 0U ? kTerrainRefDefaultGridSize : config.grid.height;
    result.cell_size_m = cubey::run_config_float_is_set(config.terrain.cell_size)
                             ? config.terrain.cell_size
                             : kTerrainRefDefaultCellSizeM;
    result.seed = config.terrain.seed_set ? config.terrain.seed : kTerrainRefDefaultSeed;
    result.vertical_scale = cubey::run_config_float_is_set(config.terrain.vertical_scale)
                                ? config.terrain.vertical_scale
                                : kTerrainRefDefaultVerticalScale;
    result.camera_preset = terrain_ref_camera_preset_from_name(
        config.terrain.camera_preset.empty() ? kTerrainRefDefaultCameraPreset
                                             : std::string_view(config.terrain.camera_preset));
    result.water_surface =
        config.terrain.water_surface >= 0 ? config.terrain.water_surface != 0 : true;

    if (result.grid_width < 2U || result.grid_height < 2U) {
        throw std::runtime_error("terrain_ref grid dimensions must be at least 2");
    }
    if (!std::isfinite(result.cell_size_m) || result.cell_size_m <= 0.0F) {
        throw std::runtime_error("terrain_ref cell size must be positive");
    }
    if (!std::isfinite(result.vertical_scale) || result.vertical_scale <= 0.0F) {
        throw std::runtime_error("terrain_ref vertical scale must be positive");
    }
    return result;
}

float terrain_ref_extent_m(const TerrainRefConfig& config) {
    return static_cast<float>(std::max(config.grid_width, config.grid_height) - 1U) *
           config.cell_size_m;
}

} // namespace cubey::projects::terrain_ref
