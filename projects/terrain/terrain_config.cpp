#include "terrain_config.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] bool has_retired_product_options(const RunConfig::TerrainOptions& terrain) {
    return !terrain.preset.empty() || !terrain.source_version.empty() ||
           !terrain.render_path.empty() || !terrain.backdrop_mesh_density.empty() ||
           cubey::run_config_float_is_set(terrain.target_edge_px) || !terrain.weathering.empty() ||
           cubey::run_config_float_is_set(terrain.weathering_strength) ||
           cubey::run_config_float_is_set(terrain.cell_size) ||
           cubey::run_config_float_is_set(terrain.sea_level) ||
           cubey::run_config_float_is_set(terrain.land_extent) ||
           cubey::run_config_float_is_set(terrain.coast_noise) ||
           cubey::run_config_float_is_set(terrain.relief) ||
           cubey::run_config_float_is_set(terrain.ridges) ||
           cubey::run_config_float_is_set(terrain.valleys) ||
           cubey::run_config_float_is_set(terrain.vertical_scale) || !terrain.recipe.empty() ||
           !terrain.study_field_path.empty() || !terrain.backdrop_profile.empty() ||
           !terrain.backdrop_center.empty() || !terrain.backdrop_mode.empty() ||
           cubey::run_config_float_is_set(terrain.backdrop_minimum_visible_distance_m) ||
           !terrain.presentation.empty() || !terrain.preview_color.empty() ||
           !terrain.preview_surface.empty() || terrain.water_surface >= 0;
}

} // namespace

std::string_view terrain_debug_view_name(TerrainDebugView view) noexcept {
    switch (view) {
    case TerrainDebugView::Surface:
        return "surface";
    case TerrainDebugView::Height:
        return "height";
    case TerrainDebugView::Slope:
        return "slope";
    case TerrainDebugView::Clay:
        return "clay";
    case TerrainDebugView::Normal:
        return "normal";
    case TerrainDebugView::MaterialWeights:
        return "material-weights";
    case TerrainDebugView::AmbientVisibility:
        return "ambient-visibility";
    case TerrainDebugView::ProjectedEdge:
        return "projected-edge";
    case TerrainDebugView::MaterialAlbedo:
        return "material-albedo";
    case TerrainDebugView::MaterialNormal:
        return "material-normal";
    case TerrainDebugView::MaterialRoughness:
        return "material-roughness";
    case TerrainDebugView::ClassificationNormal:
        return "classification-normal";
    case TerrainDebugView::StageOwnership:
        return "stage-ownership";
    }
    return "surface";
}

TerrainDebugView terrain_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "surface" || name == "final") {
        return TerrainDebugView::Surface;
    }
    if (name == "height") {
        return TerrainDebugView::Height;
    }
    if (name == "slope") {
        return TerrainDebugView::Slope;
    }
    if (name == "clay") {
        return TerrainDebugView::Clay;
    }
    if (name == "normal" || name == "normals") {
        return TerrainDebugView::Normal;
    }
    if (name == "material-weights" || name == "materials") {
        return TerrainDebugView::MaterialWeights;
    }
    if (name == "ambient-visibility" || name == "ambient") {
        return TerrainDebugView::AmbientVisibility;
    }
    if (name == "projected-edge" || name == "edge") {
        return TerrainDebugView::ProjectedEdge;
    }
    if (name == "material-albedo" || name == "albedo") {
        return TerrainDebugView::MaterialAlbedo;
    }
    if (name == "material-normal" || name == "detail-normal") {
        return TerrainDebugView::MaterialNormal;
    }
    if (name == "material-roughness" || name == "roughness") {
        return TerrainDebugView::MaterialRoughness;
    }
    if (name == "classification-normal" || name == "macro-normal") {
        return TerrainDebugView::ClassificationNormal;
    }
    if (name == "stage-ownership" || name == "ownership") {
        return TerrainDebugView::StageOwnership;
    }
    throw std::runtime_error("unsupported terrain diagnostic: " + std::string(name));
}

std::string_view terrain_placement_mode_name(TerrainPlacementMode mode) noexcept {
    switch (mode) {
    case TerrainPlacementMode::Selected:
        return "selected";
    case TerrainPlacementMode::RawCenter:
        return "raw-center";
    case TerrainPlacementMode::RawSample:
        return "raw-sample";
    }
    return "selected";
}

TerrainPlacementMode terrain_placement_mode_from_name(std::string_view name) {
    if (name.empty() || name == "selected") {
        return TerrainPlacementMode::Selected;
    }
    if (name == "raw-center") {
        return TerrainPlacementMode::RawCenter;
    }
    if (name == "raw-sample") {
        return TerrainPlacementMode::RawSample;
    }
    throw std::runtime_error("unsupported terrain placement: " + std::string(name));
}

std::string_view terrain_material_mode_name(TerrainMaterialMode mode) noexcept {
    return mode == TerrainMaterialMode::Flat ? "flat" : "filtered-detail";
}

TerrainMaterialMode terrain_material_mode_from_name(std::string_view name) {
    if (name.empty() || name == "filtered-detail" || name == "detail") {
        return TerrainMaterialMode::FilteredDetail;
    }
    if (name == "flat") {
        return TerrainMaterialMode::Flat;
    }
    throw std::runtime_error("unsupported terrain material: " + std::string(name));
}

void validate_terrain_runtime_config(const TerrainRuntimeConfig& config) {
    if (config.heightfield_path.empty()) {
        throw std::runtime_error("terrain heightfield path must not be empty");
    }
    if (!std::isfinite(config.initial_foreground_height_m) ||
        config.initial_foreground_height_m < 2.0F ||
        config.initial_foreground_height_m > 1'000.0F) {
        throw std::runtime_error("terrain foreground height must be within [2, 1000] meters");
    }
    if (config.initial_azimuth_radians.has_value() &&
        !std::isfinite(config.initial_azimuth_radians.value())) {
        throw std::runtime_error("terrain initial azimuth must be finite");
    }
    if (config.initial_orbit_radius_m.has_value() &&
        (!std::isfinite(config.initial_orbit_radius_m.value()) ||
         config.initial_orbit_radius_m.value() < 50.0F ||
         config.initial_orbit_radius_m.value() > 250.0F)) {
        throw std::runtime_error("terrain orbit radius must be within [50, 250] meters");
    }
    constexpr float maximum_elevation = 30.0F * std::numbers::pi_v<float> / 180.0F;
    if (config.initial_elevation_radians.has_value() &&
        (!std::isfinite(config.initial_elevation_radians.value()) ||
         config.initial_elevation_radians.value() < 0.0F ||
         config.initial_elevation_radians.value() > maximum_elevation)) {
        throw std::runtime_error("terrain orbit elevation must be within [0, 30] degrees");
    }
}

TerrainRuntimeConfig
terrain_runtime_config_from_run_config(const RunConfig& config,
                                       const std::filesystem::path& default_heightfield_path) {
    if (has_retired_product_options(config.terrain)) {
        throw std::runtime_error(
            "retired procedural, profile, weathering, LOD, or study options are not supported by "
            "the terrain product");
    }
    TerrainRuntimeConfig result;
    result.heightfield_path = config.terrain.heightfield_path.empty()
                                  ? default_heightfield_path
                                  : config.terrain.heightfield_path;
    if (config.terrain.seed_set) {
        result.expected_seed = config.terrain.seed;
    }
    result.placement = terrain_placement_mode_from_name(config.terrain.placement);
    result.placement_index = config.terrain.placement_index;
    if (cubey::run_config_float_is_set(config.terrain.foreground_height_m)) {
        result.initial_foreground_height_m = config.terrain.foreground_height_m;
    }
    if (!config.terrain.camera_preset.empty() && config.terrain.camera_preset != "backdrop" &&
        config.terrain.camera_preset != "backdrop-stage") {
        throw std::runtime_error("terrain product camera must be backdrop or backdrop-stage");
    }
    result.foreground_sphere = config.terrain.camera_preset != "backdrop";
    result.material = terrain_material_mode_from_name(config.terrain.surface_detail);
    result.debug_view = terrain_debug_view_from_name(config.debug_view);
    constexpr float degrees_to_radians = std::numbers::pi_v<float> / 180.0F;
    if (cubey::run_config_float_is_set(config.terrain.backdrop_azimuth_degrees)) {
        result.initial_azimuth_radians =
            config.terrain.backdrop_azimuth_degrees * degrees_to_radians;
    }
    if (cubey::run_config_float_is_set(config.terrain.backdrop_orbit_radius_m)) {
        result.initial_orbit_radius_m = config.terrain.backdrop_orbit_radius_m;
    }
    if (cubey::run_config_float_is_set(config.terrain.backdrop_elevation_degrees)) {
        result.initial_elevation_radians =
            config.terrain.backdrop_elevation_degrees * degrees_to_radians;
    }
    validate_terrain_runtime_config(result);
    return result;
}

} // namespace cubey::projects::terrain
