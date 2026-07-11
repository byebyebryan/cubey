#include "terrain_config.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain {

std::string_view terrain_camera_preset_name(TerrainCameraPreset preset) {
    switch (preset) {
    case TerrainCameraPreset::Oblique:
        return "oblique";
    case TerrainCameraPreset::Profile:
        return "profile";
    case TerrainCameraPreset::Top:
        return "top";
    case TerrainCameraPreset::Surface:
        return "surface";
    case TerrainCameraPreset::SurfaceLow:
        return "surface-low";
    }
    throw std::runtime_error("unknown terrain camera preset");
}

TerrainCameraPreset terrain_camera_preset_from_name(std::string_view name) {
    if (name.empty() || name == "oblique") {
        return TerrainCameraPreset::Oblique;
    }
    if (name == "profile") {
        return TerrainCameraPreset::Profile;
    }
    if (name == "top") {
        return TerrainCameraPreset::Top;
    }
    if (name == "surface") {
        return TerrainCameraPreset::Surface;
    }
    if (name == "surface-low") {
        return TerrainCameraPreset::SurfaceLow;
    }
    throw std::runtime_error("unknown terrain camera preset: " + std::string(name));
}

std::string_view terrain_debug_view_name(TerrainDebugView view) {
    switch (view) {
    case TerrainDebugView::Surface:
        return "surface";
    case TerrainDebugView::Height:
        return "height";
    case TerrainDebugView::BaseHeight:
        return "base-height";
    case TerrainDebugView::Slope:
        return "slope";
    case TerrainDebugView::Weathering:
        return "weathering";
    case TerrainDebugView::Lod:
        return "lod";
    }
    throw std::runtime_error("unknown terrain debug view");
}

TerrainDebugView terrain_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "surface" || name == "final") {
        return TerrainDebugView::Surface;
    }
    if (name == "height") {
        return TerrainDebugView::Height;
    }
    if (name == "base-height" || name == "base") {
        return TerrainDebugView::BaseHeight;
    }
    if (name == "slope") {
        return TerrainDebugView::Slope;
    }
    if (name == "weathering" || name == "erosion") {
        return TerrainDebugView::Weathering;
    }
    if (name == "lod") {
        return TerrainDebugView::Lod;
    }
    throw std::runtime_error("unknown terrain debug view: " + std::string(name));
}

void validate_terrain_runtime_config(const TerrainRuntimeConfig& config) {
    validate_terrain_source_config(config.source);
    if (!std::isfinite(config.near_cell_size_m) || config.near_cell_size_m <= 0.0F ||
        !std::isfinite(config.vertical_scale) || config.vertical_scale <= 0.0F ||
        config.lod_levels == 0U || config.lod_levels > 12U || config.cells_per_axis < 16U) {
        throw std::runtime_error("invalid terrain runtime configuration");
    }
}

TerrainRuntimeConfig terrain_runtime_config_from_run_config(const RunConfig& config) {
    TerrainRuntimeConfig result{};
    result.source.seed = config.terrain.seed_set ? config.terrain.seed : kTerrainDefaultSeed;
    result.source.preset = terrain_preset_from_name(config.terrain.preset);
    result.source.weathering = config.terrain.weathering.empty()
                                   ? TerrainWeatheringMode::Local
                                   : terrain_weathering_mode_from_name(config.terrain.weathering);
    result.source.weathering_strength =
        cubey::run_config_float_is_set(config.terrain.weathering_strength)
            ? config.terrain.weathering_strength
            : 1.0F;
    result.camera = terrain_camera_preset_from_name(config.terrain.camera_preset);
    result.debug_view = terrain_debug_view_from_name(config.debug_view);
    result.near_cell_size_m =
        cubey::run_config_float_is_set(config.terrain.cell_size) ? config.terrain.cell_size : 2.0F;
    result.vertical_scale = cubey::run_config_float_is_set(config.terrain.vertical_scale)
                                ? config.terrain.vertical_scale
                                : 1.0F;
    validate_terrain_runtime_config(result);
    return result;
}

} // namespace cubey::projects::terrain
