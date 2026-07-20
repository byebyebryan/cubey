#include "terrain_config.h"

#include <cmath>
#include <numbers>
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
    case TerrainCameraPreset::Ground:
        return "ground";
    case TerrainCameraPreset::Backdrop:
        return "backdrop";
    case TerrainCameraPreset::BackdropStage:
        return "backdrop-stage";
    case TerrainCameraPreset::Midground:
        return "midground";
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
    if (name == "ground") {
        return TerrainCameraPreset::Ground;
    }
    if (name == "backdrop") {
        return TerrainCameraPreset::Backdrop;
    }
    if (name == "backdrop-stage") {
        return TerrainCameraPreset::BackdropStage;
    }
    if (name == "midground") {
        return TerrainCameraPreset::Midground;
    }
    throw std::runtime_error("unknown terrain camera preset: " + std::string(name));
}

bool terrain_camera_is_surface(TerrainCameraPreset preset) noexcept {
    return preset == TerrainCameraPreset::Surface || preset == TerrainCameraPreset::SurfaceLow ||
           preset == TerrainCameraPreset::Ground || preset == TerrainCameraPreset::Midground;
}

bool terrain_camera_is_backdrop(TerrainCameraPreset preset) noexcept {
    return preset == TerrainCameraPreset::Backdrop || preset == TerrainCameraPreset::BackdropStage;
}

bool terrain_camera_advances_headless(TerrainCameraPreset preset) noexcept {
    return preset == TerrainCameraPreset::Surface || preset == TerrainCameraPreset::SurfaceLow ||
           preset == TerrainCameraPreset::Ground || preset == TerrainCameraPreset::Midground;
}

std::string_view terrain_backdrop_profile_name(TerrainBackdropProfile profile) noexcept {
    switch (profile) {
    case TerrainBackdropProfile::HardCutV1:
        return "hard-cut-v1";
    case TerrainBackdropProfile::RadialV1:
        return "radial-v1";
    case TerrainBackdropProfile::RasterV1:
        return "raster-v1";
    }
    return "hard-cut-v1";
}

TerrainBackdropProfile terrain_backdrop_profile_from_name(std::string_view name) {
    if (name.empty() || name == "radial-v1") {
        return TerrainBackdropProfile::RadialV1;
    }
    if (name == "hard-cut-v1") {
        return TerrainBackdropProfile::HardCutV1;
    }
    if (name == "raster-v1") {
        return TerrainBackdropProfile::RasterV1;
    }
    throw std::runtime_error("unknown terrain backdrop profile: " + std::string(name));
}

std::string_view
terrain_backdrop_center_ownership_name(TerrainBackdropCenterOwnership ownership) noexcept {
    switch (ownership) {
    case TerrainBackdropCenterOwnership::ConsumerOwned:
        return "consumer-owned";
    case TerrainBackdropCenterOwnership::Continuous:
        return "continuous";
    }
    return "consumer-owned";
}

TerrainBackdropCenterOwnership terrain_backdrop_center_ownership_from_name(std::string_view name) {
    if (name.empty() || name == "consumer-owned") {
        return TerrainBackdropCenterOwnership::ConsumerOwned;
    }
    if (name == "continuous") {
        return TerrainBackdropCenterOwnership::Continuous;
    }
    throw std::runtime_error("unknown terrain backdrop center ownership: " + std::string(name));
}

float terrain_camera_clearance_m(TerrainCameraPreset preset) {
    switch (preset) {
    case TerrainCameraPreset::Surface:
        return 70.0F;
    case TerrainCameraPreset::SurfaceLow:
        return 18.0F;
    case TerrainCameraPreset::Ground:
        return 2.0F;
    case TerrainCameraPreset::Backdrop:
    case TerrainCameraPreset::BackdropStage:
    case TerrainCameraPreset::Midground:
        return 150.0F;
    case TerrainCameraPreset::Oblique:
    case TerrainCameraPreset::Profile:
    case TerrainCameraPreset::Top:
        break;
    }
    throw std::runtime_error("terrain orbit camera does not have a surface clearance");
}

float terrain_camera_traversal_speed_mps(TerrainCameraPreset preset) noexcept {
    if (preset == TerrainCameraPreset::Ground) {
        return 12.0F;
    }
    return terrain_camera_is_backdrop(preset) || preset == TerrainCameraPreset::Midground ? 80.0F
                                                                                          : 220.0F;
}

float terrain_camera_fovy_radians(TerrainCameraPreset preset) noexcept {
    constexpr float degrees_to_radians = std::numbers::pi_v<float> / 180.0F;
    return (terrain_camera_is_backdrop(preset) || preset == TerrainCameraPreset::Midground
                ? 40.0F
                : 60.0F) *
           degrees_to_radians;
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
    case TerrainDebugView::Clay:
        return "clay";
    case TerrainDebugView::Shadow:
        return "shadow";
    case TerrainDebugView::AerialTransmittance:
        return "aerial-transmittance";
    case TerrainDebugView::VegetationCoverage:
        return "vegetation-coverage";
    case TerrainDebugView::Normal:
        return "normal";
    case TerrainDebugView::MaterialWeights:
        return "material-weights";
    case TerrainDebugView::AmbientVisibility:
        return "ambient-visibility";
    case TerrainDebugView::TessellationFactor:
        return "tessellation-factor";
    case TerrainDebugView::ProjectedEdge:
        return "projected-edge";
    case TerrainDebugView::MaterialAlbedo:
        return "material-albedo";
    case TerrainDebugView::MaterialNormal:
        return "material-normal";
    case TerrainDebugView::SourceBands:
        return "source-bands";
    case TerrainDebugView::MaterialRoughness:
        return "material-roughness";
    case TerrainDebugView::MaterialHeight:
        return "material-height";
    case TerrainDebugView::MaterialCavity:
        return "material-cavity";
    case TerrainDebugView::ClassificationNormal:
        return "classification-normal";
    case TerrainDebugView::SourceRange:
        return "source-range";
    case TerrainDebugView::SourceMassif:
        return "source-massif";
    case TerrainDebugView::SourceValley:
        return "source-valley";
    case TerrainDebugView::SourceRidge:
        return "source-ridge";
    case TerrainDebugView::SourceMeso:
        return "source-meso";
    case TerrainDebugView::StageOwnership:
        return "stage-ownership";
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
    if (name == "clay") {
        return TerrainDebugView::Clay;
    }
    if (name == "shadow") {
        return TerrainDebugView::Shadow;
    }
    if (name == "aerial-transmittance" || name == "aerial") {
        return TerrainDebugView::AerialTransmittance;
    }
    if (name == "vegetation-coverage" || name == "vegetation" || name == "coverage") {
        return TerrainDebugView::VegetationCoverage;
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
    if (name == "tessellation-factor" || name == "tessellation" || name == "tess") {
        return TerrainDebugView::TessellationFactor;
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
    if (name == "source-bands" || name == "bands") {
        return TerrainDebugView::SourceBands;
    }
    if (name == "material-roughness" || name == "roughness") {
        return TerrainDebugView::MaterialRoughness;
    }
    if (name == "material-height" || name == "blend-height") {
        return TerrainDebugView::MaterialHeight;
    }
    if (name == "material-cavity" || name == "cavity") {
        return TerrainDebugView::MaterialCavity;
    }
    if (name == "classification-normal" || name == "macro-normal") {
        return TerrainDebugView::ClassificationNormal;
    }
    if (name == "source-range" || name == "range-support") {
        return TerrainDebugView::SourceRange;
    }
    if (name == "source-massif" || name == "massif") {
        return TerrainDebugView::SourceMassif;
    }
    if (name == "source-valley" || name == "valley-delta") {
        return TerrainDebugView::SourceValley;
    }
    if (name == "source-ridge" || name == "ridge-delta") {
        return TerrainDebugView::SourceRidge;
    }
    if (name == "source-meso" || name == "meso-delta") {
        return TerrainDebugView::SourceMeso;
    }
    if (name == "stage-ownership" || name == "ownership") {
        return TerrainDebugView::StageOwnership;
    }
    throw std::runtime_error("unknown terrain debug view: " + std::string(name));
}

std::string_view terrain_presentation_mode_name(TerrainPresentationMode mode) {
    switch (mode) {
    case TerrainPresentationMode::Standard:
        return "standard";
    case TerrainPresentationMode::Backdrop:
        return "backdrop";
    }
    throw std::runtime_error("unknown terrain presentation mode");
}

TerrainPresentationMode terrain_presentation_mode_from_name(std::string_view name) {
    if (name.empty() || name == "standard") {
        return TerrainPresentationMode::Standard;
    }
    if (name == "backdrop") {
        return TerrainPresentationMode::Backdrop;
    }
    throw std::runtime_error("unknown terrain presentation mode: " + std::string(name));
}

std::string_view terrain_render_path_name(TerrainRenderPath path) {
    switch (path) {
    case TerrainRenderPath::Control:
        return "control";
    case TerrainRenderPath::Quality:
        return "quality";
    case TerrainRenderPath::Backdrop:
        return "backdrop";
    }
    throw std::runtime_error("unknown terrain render path");
}

TerrainRenderPath terrain_render_path_from_name(std::string_view name) {
    if (name.empty() || name == "control") {
        return TerrainRenderPath::Control;
    }
    if (name == "quality") {
        return TerrainRenderPath::Quality;
    }
    if (name == "backdrop") {
        return TerrainRenderPath::Backdrop;
    }
    throw std::runtime_error("unknown terrain render path: " + std::string(name));
}

std::string_view terrain_surface_detail_name(TerrainSurfaceDetail detail) {
    switch (detail) {
    case TerrainSurfaceDetail::Tile:
        return "tile";
    case TerrainSurfaceDetail::Layered:
        return "layered";
    }
    throw std::runtime_error("unknown terrain surface detail");
}

TerrainSurfaceDetail terrain_surface_detail_from_name(std::string_view name) {
    if (name.empty() || name == "tile") {
        return TerrainSurfaceDetail::Tile;
    }
    if (name == "layered") {
        return TerrainSurfaceDetail::Layered;
    }
    throw std::runtime_error("unknown terrain surface detail: " + std::string(name));
}

void validate_terrain_runtime_config(const TerrainRuntimeConfig& config) {
    validate_terrain_source_config(config.source);
    if (!std::isfinite(config.near_cell_size_m) || config.near_cell_size_m <= 0.0F ||
        !std::isfinite(config.vertical_scale) || config.vertical_scale <= 0.0F ||
        !std::isfinite(config.target_edge_px) || config.target_edge_px < 2.0F ||
        config.target_edge_px > 16.0F || config.lod_levels == 0U || config.lod_levels > 12U ||
        config.cells_per_axis < 16U) {
        throw std::runtime_error("invalid terrain runtime configuration");
    }
    if (config.render_path == TerrainRenderPath::Quality &&
        config.source.preset != TerrainPreset::Mountain) {
        throw std::runtime_error("quality terrain rendering currently supports only mountain");
    }
    if (config.surface_detail == TerrainSurfaceDetail::Layered &&
        config.render_path != TerrainRenderPath::Quality) {
        throw std::runtime_error("layered terrain surface detail requires quality rendering");
    }
    if (config.render_path == TerrainRenderPath::Backdrop &&
        !terrain_camera_is_backdrop(config.camera)) {
        throw std::runtime_error("cached terrain backdrop rendering requires a backdrop camera");
    }
    constexpr float degrees_to_radians = std::numbers::pi_v<float> / 180.0F;
    const bool radial = config.render_path == TerrainRenderPath::Backdrop &&
                        config.backdrop_profile == TerrainBackdropProfile::RadialV1;
    const bool raster = config.render_path == TerrainRenderPath::Backdrop &&
                        config.backdrop_profile == TerrainBackdropProfile::RasterV1;
    const bool frozen_backdrop = radial || raster;
    const float minimum_elevation_radians =
        frozen_backdrop
            ? 0.0F
            : (config.backdrop_mode == TerrainBackdropStageMode::Detached ? 0.0F : 12.0F) *
                  degrees_to_radians;
    const float maximum_elevation_radians =
        (frozen_backdrop
             ? 30.0F
             : (config.backdrop_mode == TerrainBackdropStageMode::Detached ? 30.0F : 32.0F)) *
        degrees_to_radians;
    const float minimum_orbit_radius_m = radial ? 100.0F : 50.0F;
    const float maximum_orbit_radius_m = radial ? 1'000.0F : 250.0F;
    if ((config.backdrop_azimuth_radians.has_value() &&
         !std::isfinite(config.backdrop_azimuth_radians.value())) ||
        (config.backdrop_orbit_radius_m.has_value() &&
         (!std::isfinite(config.backdrop_orbit_radius_m.value()) ||
          config.backdrop_orbit_radius_m.value() < minimum_orbit_radius_m ||
          config.backdrop_orbit_radius_m.value() > maximum_orbit_radius_m)) ||
        (config.backdrop_elevation_radians.has_value() &&
         (!std::isfinite(config.backdrop_elevation_radians.value()) ||
          config.backdrop_elevation_radians.value() < minimum_elevation_radians ||
          config.backdrop_elevation_radians.value() > maximum_elevation_radians)) ||
        !std::isfinite(config.backdrop_minimum_visible_distance_m) ||
        (radial ? config.backdrop_minimum_visible_distance_m != 6'000.0F
                : (raster ? config.backdrop_minimum_visible_distance_m != 3'200.0F
                          : config.backdrop_minimum_visible_distance_m < 750.0F ||
                                config.backdrop_minimum_visible_distance_m > 6'000.0F))) {
        throw std::runtime_error("invalid terrain backdrop orbit configuration");
    }
    if (config.render_path == TerrainRenderPath::Backdrop &&
        config.backdrop_profile == TerrainBackdropProfile::HardCutV1 &&
        config.backdrop_center == TerrainBackdropCenterOwnership::Continuous) {
        throw std::runtime_error("hard-cut-v1 requires a consumer-owned terrain center");
    }
    if (raster && config.backdrop_center != TerrainBackdropCenterOwnership::Continuous) {
        throw std::runtime_error("raster-v1 requires a continuous terrain center");
    }
    if (raster && config.heightfield_path.empty()) {
        throw std::runtime_error("raster-v1 requires a terrain heightfield");
    }
    if (!raster && !config.heightfield_path.empty()) {
        throw std::runtime_error("terrain heightfield requires raster-v1");
    }
    const bool v3_component_view = config.debug_view == TerrainDebugView::SourceRange ||
                                   config.debug_view == TerrainDebugView::SourceMassif ||
                                   config.debug_view == TerrainDebugView::SourceValley ||
                                   config.debug_view == TerrainDebugView::SourceRidge ||
                                   config.debug_view == TerrainDebugView::SourceMeso;
    if (v3_component_view && config.source.version != TerrainSourceVersion::V3) {
        throw std::runtime_error("terrain source component views require source v3");
    }
}

TerrainRuntimeConfig terrain_runtime_config_from_run_config(const RunConfig& config) {
    if (!config.terrain.study_field_path.empty()) {
        throw std::runtime_error(
            "terrain study field is supported only by terrain_external_source_study");
    }
    TerrainRuntimeConfig result{};
    result.heightfield_path = config.terrain.heightfield_path;
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
    result.render_path =
        config.terrain.render_path.empty() && terrain_camera_is_backdrop(result.camera)
            ? TerrainRenderPath::Backdrop
            : terrain_render_path_from_name(config.terrain.render_path);
    if (result.render_path == TerrainRenderPath::Backdrop) {
        result.backdrop_profile =
            terrain_backdrop_profile_from_name(config.terrain.backdrop_profile);
        result.backdrop_center =
            config.terrain.backdrop_center.empty()
                ? (result.backdrop_profile != TerrainBackdropProfile::HardCutV1
                       ? TerrainBackdropCenterOwnership::Continuous
                       : TerrainBackdropCenterOwnership::ConsumerOwned)
                : terrain_backdrop_center_ownership_from_name(config.terrain.backdrop_center);
    } else if (!config.terrain.backdrop_profile.empty() ||
               !config.terrain.backdrop_center.empty() ||
               !config.terrain.heightfield_path.empty()) {
        throw std::runtime_error(
            "terrain backdrop profile and center require the cached backdrop render path");
    }
    result.source.version =
        config.terrain.source_version.empty() && terrain_camera_is_backdrop(result.camera)
            ? TerrainSourceVersion::V2_1
            : terrain_source_version_from_name(config.terrain.source_version);
    const bool frozen_backdrop = result.render_path == TerrainRenderPath::Backdrop &&
                                 result.backdrop_profile != TerrainBackdropProfile::HardCutV1;
    const bool raster_backdrop = result.render_path == TerrainRenderPath::Backdrop &&
                                 result.backdrop_profile == TerrainBackdropProfile::RasterV1;
    if (frozen_backdrop) {
        if (!config.terrain.preset.empty() || !config.terrain.source_version.empty() ||
            !config.terrain.surface_detail.empty() ||
            cubey::run_config_float_is_set(config.terrain.target_edge_px) ||
            cubey::run_config_float_is_set(config.terrain.weathering_strength) ||
            cubey::run_config_float_is_set(config.terrain.cell_size) ||
            cubey::run_config_float_is_set(config.terrain.sea_level) ||
            cubey::run_config_float_is_set(config.terrain.land_extent) ||
            cubey::run_config_float_is_set(config.terrain.coast_noise) ||
            cubey::run_config_float_is_set(config.terrain.relief) ||
            cubey::run_config_float_is_set(config.terrain.ridges) ||
            cubey::run_config_float_is_set(config.terrain.valleys) ||
            !config.terrain.recipe.empty() ||
            !config.terrain.preview_color.empty() || !config.terrain.preview_surface.empty() ||
            config.terrain.water_surface >= 0) {
            throw std::runtime_error(
                "frozen terrain backdrop owns its source and rejects source or quality overrides");
        }
        const float required_minimum_distance_m = raster_backdrop ? 3'200.0F : 6'000.0F;
        if ((!config.terrain.weathering.empty() && config.terrain.weathering != "off") ||
            (!config.terrain.backdrop_mesh_density.empty() &&
             config.terrain.backdrop_mesh_density != "high") ||
            (!config.terrain.backdrop_mode.empty() && config.terrain.backdrop_mode != "grounded") ||
            (cubey::run_config_float_is_set(config.terrain.backdrop_minimum_visible_distance_m) &&
             config.terrain.backdrop_minimum_visible_distance_m != required_minimum_distance_m) ||
            (cubey::run_config_float_is_set(config.terrain.vertical_scale) &&
             config.terrain.vertical_scale != 1.0F) ||
            (raster_backdrop && !config.terrain.backdrop_center.empty() &&
             config.terrain.backdrop_center != "continuous") ||
            (raster_backdrop && !config.terrain.presentation.empty() &&
             config.terrain.presentation != "backdrop")) {
            throw std::runtime_error(
                "terrain backdrop profile rejects overrides of its frozen product contract");
        }
        if (raster_backdrop && config.terrain.heightfield_path.empty()) {
            throw std::runtime_error("raster-v1 requires --terrain-heightfield");
        }
        if (!raster_backdrop && !config.terrain.heightfield_path.empty()) {
            throw std::runtime_error("--terrain-heightfield requires raster-v1");
        }
        result.source.weathering = TerrainWeatheringMode::Off;
        result.backdrop_mode = TerrainBackdropStageMode::Grounded;
        result.backdrop_minimum_visible_distance_m = required_minimum_distance_m;
        result.backdrop_mesh_density = TerrainBackdropMeshDensity::High;
        result.vertical_scale = 1.0F;
    } else if (config.terrain.backdrop_mode.empty() || config.terrain.backdrop_mode == "detached") {
        result.backdrop_mode = TerrainBackdropStageMode::Detached;
    } else if (config.terrain.backdrop_mode == "grounded") {
        result.backdrop_mode = TerrainBackdropStageMode::Grounded;
    } else {
        throw std::runtime_error("unknown terrain backdrop stage mode: " +
                                 config.terrain.backdrop_mode);
    }
    constexpr float degrees_to_radians = std::numbers::pi_v<float> / 180.0F;
    if (cubey::run_config_float_is_set(config.terrain.backdrop_azimuth_degrees)) {
        result.backdrop_azimuth_radians =
            config.terrain.backdrop_azimuth_degrees * degrees_to_radians;
    }
    if (cubey::run_config_float_is_set(config.terrain.backdrop_orbit_radius_m)) {
        result.backdrop_orbit_radius_m = config.terrain.backdrop_orbit_radius_m;
    }
    if (cubey::run_config_float_is_set(config.terrain.backdrop_elevation_degrees)) {
        result.backdrop_elevation_radians =
            config.terrain.backdrop_elevation_degrees * degrees_to_radians;
    }
    if (cubey::run_config_float_is_set(config.terrain.backdrop_minimum_visible_distance_m)) {
        result.backdrop_minimum_visible_distance_m =
            config.terrain.backdrop_minimum_visible_distance_m;
    }
    result.debug_view = terrain_debug_view_from_name(config.debug_view);
    result.presentation = config.terrain.presentation.empty() &&
                                  result.render_path == TerrainRenderPath::Backdrop &&
                                  result.backdrop_profile != TerrainBackdropProfile::HardCutV1
                              ? TerrainPresentationMode::Backdrop
                              : terrain_presentation_mode_from_name(config.terrain.presentation);
    if (!frozen_backdrop) {
        result.backdrop_mesh_density =
            terrain_backdrop_mesh_density_from_name(config.terrain.backdrop_mesh_density);
    }
    result.surface_detail = terrain_surface_detail_from_name(config.terrain.surface_detail);
    result.target_edge_px = cubey::run_config_float_is_set(config.terrain.target_edge_px)
                                ? config.terrain.target_edge_px
                                : 4.0F;
    result.near_cell_size_m =
        cubey::run_config_float_is_set(config.terrain.cell_size) ? config.terrain.cell_size : 2.0F;
    if (!frozen_backdrop) {
        result.vertical_scale = cubey::run_config_float_is_set(config.terrain.vertical_scale)
                                    ? config.terrain.vertical_scale
                                    : 1.0F;
    }
    validate_terrain_runtime_config(result);
    return result;
}

} // namespace cubey::projects::terrain
