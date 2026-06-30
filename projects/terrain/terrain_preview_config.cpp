#include "terrain_preview_config.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain {

std::string_view terrain_preview_camera_preset_name(TerrainPreviewCameraPreset preset) {
    switch (preset) {
    case TerrainPreviewCameraPreset::Oblique:
        return "oblique";
    case TerrainPreviewCameraPreset::Profile:
        return "profile";
    case TerrainPreviewCameraPreset::Top:
        return "top";
    }
    return "oblique";
}

TerrainPreviewCameraPreset terrain_preview_camera_preset_from_name(std::string_view name) {
    if (name == "oblique") {
        return TerrainPreviewCameraPreset::Oblique;
    }
    if (name == "profile") {
        return TerrainPreviewCameraPreset::Profile;
    }
    if (name == "top") {
        return TerrainPreviewCameraPreset::Top;
    }
    throw std::runtime_error("terrain camera preset must be oblique, profile, or top");
}

std::string_view terrain_preview_color_mode_name(TerrainPreviewColorMode mode) {
    switch (mode) {
    case TerrainPreviewColorMode::Material:
        return "material";
    case TerrainPreviewColorMode::Height:
        return "height";
    case TerrainPreviewColorMode::River:
        return "river";
    case TerrainPreviewColorMode::Channel:
        return "channel";
    }
    return "material";
}

TerrainPreviewColorMode terrain_preview_color_mode_from_name(std::string_view name) {
    if (name == "material") {
        return TerrainPreviewColorMode::Material;
    }
    if (name == "height") {
        return TerrainPreviewColorMode::Height;
    }
    if (name == "river") {
        return TerrainPreviewColorMode::River;
    }
    if (name == "channel") {
        return TerrainPreviewColorMode::Channel;
    }
    throw std::runtime_error(
        "terrain preview color mode must be material, height, river, or channel");
}

TerrainPreviewConfig terrain_preview_config_from_run_config(const cubey::RunConfig& config) {
    TerrainPreviewConfig preview;
    preview.region.grid_width =
        config.grid.width == 0U ? kTerrainDefaultGridSize : config.grid.width;
    preview.region.grid_height =
        config.grid.height == 0U ? kTerrainDefaultGridSize : config.grid.height;
    preview.region.cell_size_m = cubey::run_config_float_is_set(config.terrain.cell_size)
                                     ? config.terrain.cell_size
                                     : kTerrainDefaultCellSizeM;
    preview.region.seed =
        config.terrain.seed_set ? config.terrain.seed : kTerrainDefaultSeed;
    preview.region.recipe_id =
        config.terrain.recipe.empty() ? std::string(kTerrainPreviewDefaultRecipe)
                                      : config.terrain.recipe;
    preview.region.generator_revision = kTerrainGeneratorRevision;

    preview.camera_preset = terrain_preview_camera_preset_from_name(
        config.terrain.camera_preset.empty() ? kTerrainPreviewDefaultCameraPreset
                                             : std::string_view(config.terrain.camera_preset));
    preview.color_mode = terrain_preview_color_mode_from_name(
        config.terrain.preview_color.empty() ? kTerrainPreviewDefaultColorMode
                                             : std::string_view(config.terrain.preview_color));
    preview.vertical_scale = cubey::run_config_float_is_set(config.terrain.vertical_scale)
                                 ? config.terrain.vertical_scale
                                 : kTerrainPreviewDefaultVerticalScale;
    if (!std::isfinite(preview.vertical_scale) || preview.vertical_scale <= 0.0F) {
        throw std::runtime_error("terrain preview vertical scale must be positive");
    }

    validate_terrain_region_config(preview.region);
    return preview;
}

} // namespace cubey::projects::terrain
