#include "terrain_ref_config.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain_ref {

std::string_view terrain_ref_recipe_name(TerrainRefRecipe recipe) {
    switch (recipe) {
    case TerrainRefRecipe::TerrainEngine:
        return kTerrainRefRecipeTerrainEngine;
    case TerrainRefRecipe::ShadertoyMountain:
        return kTerrainRefRecipeShadertoyMountain;
    case TerrainRefRecipe::ShadertoyAlpine:
        return kTerrainRefRecipeShadertoyAlpine;
    case TerrainRefRecipe::ShadertoyDunes:
        return kTerrainRefRecipeShadertoyDunes;
    case TerrainRefRecipe::ShadertoyLakeBasin:
        return kTerrainRefRecipeShadertoyLakeBasin;
    case TerrainRefRecipe::ShadertoyBadlands:
        return kTerrainRefRecipeShadertoyBadlands;
    case TerrainRefRecipe::ShadertoyCoastIsland:
        return kTerrainRefRecipeShadertoyCoastIsland;
    }
    return kTerrainRefRecipeTerrainEngine;
}

TerrainRefRecipe terrain_ref_recipe_from_name(std::string_view name) {
    if (name.empty() || is_terrain_engine_reference_recipe(name)) {
        return TerrainRefRecipe::TerrainEngine;
    }
    if (name == kTerrainRefRecipeShadertoyMountain) {
        return TerrainRefRecipe::ShadertoyMountain;
    }
    if (name == kTerrainRefRecipeShadertoyAlpine) {
        return TerrainRefRecipe::ShadertoyAlpine;
    }
    if (name == kTerrainRefRecipeShadertoyDunes) {
        return TerrainRefRecipe::ShadertoyDunes;
    }
    if (name == kTerrainRefRecipeShadertoyLakeBasin) {
        return TerrainRefRecipe::ShadertoyLakeBasin;
    }
    if (name == kTerrainRefRecipeShadertoyBadlands) {
        return TerrainRefRecipe::ShadertoyBadlands;
    }
    if (name == kTerrainRefRecipeShadertoyCoastIsland) {
        return TerrainRefRecipe::ShadertoyCoastIsland;
    }
    throw std::runtime_error("terrain_ref recipe must be terrain-engine-ref, "
                             "shadertoy-mountain, shadertoy-alpine, "
                             "shadertoy-dunes, shadertoy-lake-basin, "
                             "shadertoy-badlands, or shadertoy-coast-island");
}

TerrainRefMaterialMode terrain_ref_material_mode_from_name(std::string_view name) {
    if (name.empty() || name == "material") {
        return TerrainRefMaterialMode::Recipe;
    }
    if (name == "height") {
        return TerrainRefMaterialMode::Height;
    }
    throw std::runtime_error("terrain_ref preview color must be material or height");
}

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
    case TerrainRefCameraPreset::CoastalOblique:
        return "coastal-oblique";
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
    if (name == "coastal-oblique" || name == "coastal_oblique") {
        return TerrainRefCameraPreset::CoastalOblique;
    }
    throw std::runtime_error(
        "terrain_ref camera preset must be oblique, profile, top, surface, surface-low, "
        "or coastal-oblique");
}

TerrainRefConfig terrain_ref_config_from_run_config(const cubey::RunConfig& config) {
    TerrainRefConfig result;
    result.recipe = terrain_ref_recipe_from_name(config.terrain.recipe);
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
    result.material_mode = terrain_ref_material_mode_from_name(config.terrain.preview_color);
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
