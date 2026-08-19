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
    case TerrainRefRecipe::ShadertoyPlains:
        return kTerrainRefRecipeShadertoyPlains;
    case TerrainRefRecipe::ShadertoyGorge:
        return kTerrainRefRecipeShadertoyGorge;
    case TerrainRefRecipe::ShadertoyGlacialHighland:
        return kTerrainRefRecipeShadertoyGlacialHighland;
    case TerrainRefRecipe::ShadertoyCraterField:
        return kTerrainRefRecipeShadertoyCraterField;
    case TerrainRefRecipe::ShadertoyErosionFilter:
        return kTerrainRefRecipeShadertoyErosionFilter;
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
    if (name == kTerrainRefRecipeShadertoyPlains) {
        return TerrainRefRecipe::ShadertoyPlains;
    }
    if (name == kTerrainRefRecipeShadertoyGorge) {
        return TerrainRefRecipe::ShadertoyGorge;
    }
    if (name == kTerrainRefRecipeShadertoyGlacialHighland) {
        return TerrainRefRecipe::ShadertoyGlacialHighland;
    }
    if (name == kTerrainRefRecipeShadertoyCraterField) {
        return TerrainRefRecipe::ShadertoyCraterField;
    }
    if (name == kTerrainRefRecipeShadertoyErosionFilter) {
        return TerrainRefRecipe::ShadertoyErosionFilter;
    }
    throw std::runtime_error("terrain_ref recipe must be terrain-engine-ref, "
                             "shadertoy-mountain, shadertoy-alpine, "
                             "shadertoy-dunes, shadertoy-lake-basin, "
                             "shadertoy-badlands, shadertoy-coast-island, "
                             "shadertoy-plains, shadertoy-gorge, "
                             "shadertoy-glacial-highland, shadertoy-crater-field, or "
                             "shadertoy-erosion-filter");
}

TerrainRefMaterialMode terrain_ref_material_mode_from_name(std::string_view name) {
    if (name.empty() || name == "material") {
        return TerrainRefMaterialMode::Recipe;
    }
    if (name == "height") {
        return TerrainRefMaterialMode::Height;
    }
    if (name == "erosion") {
        return TerrainRefMaterialMode::Erosion;
    }
    throw std::runtime_error("terrain_ref preview color must be material, height, or erosion");
}

TerrainRefSurfaceMode terrain_ref_surface_mode_from_name(std::string_view name) {
    if (name.empty() || name == "height" || name == "post-erosion") {
        return TerrainRefSurfaceMode::Filtered;
    }
    if (name == "pre-process") {
        return TerrainRefSurfaceMode::Base;
    }
    throw std::runtime_error(
        "terrain_ref preview surface must be height, post-erosion, or pre-process");
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

float terrain_ref_extent_m(const TerrainRefConfig& config) {
    return static_cast<float>(std::max(config.grid_width, config.grid_height) - 1U) *
           config.cell_size_m;
}

} // namespace cubey::projects::terrain_ref
