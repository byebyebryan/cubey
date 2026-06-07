#include "planet_config.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace cubey::projects::planet {

PlanetConfig planet_config_for_scale_preset(PlanetScalePreset preset) {
    PlanetConfig config{};
    apply_planet_scale_preset(config, preset);
    return config;
}

void apply_planet_scale_preset(PlanetConfig& config, PlanetScalePreset preset) {
    config.scale_preset = preset;
    switch (preset) {
    case PlanetScalePreset::Earthlike:
        config.radius_m = kPlanetEarthlikeRadiusM;
        config.atmosphere_height_m = kPlanetEarthlikeAtmosphereHeightM;
        config.camera_altitude_m = kPlanetEarthlikeCameraAltitudeM;
        break;
    case PlanetScalePreset::Mini:
        config.radius_m = kPlanetMiniRadiusM;
        config.atmosphere_height_m = kPlanetMiniAtmosphereHeightM;
        config.camera_altitude_m = kPlanetMiniCameraAltitudeM;
        break;
    }
}

PlanetConfig planet_config_from_run_config(const RunConfig& config) {
    PlanetConfig planet{};
    if (!config.planet.scale_preset.empty()) {
        apply_planet_scale_preset(planet,
                                  planet_scale_preset_from_string(config.planet.scale_preset));
    }
    if (run_config_float_is_set(config.planet.radius_m)) {
        planet.radius_m = config.planet.radius_m;
    }
    if (run_config_float_is_set(config.planet.atmosphere_height_m)) {
        planet.atmosphere_height_m = config.planet.atmosphere_height_m;
    }
    if (run_config_float_is_set(config.planet.camera_altitude_m)) {
        planet.camera_altitude_m = config.planet.camera_altitude_m;
    }
    if (config.planet.patches_per_face != 0U) {
        planet.patches_per_face = config.planet.patches_per_face;
    }
    if (config.planet.patch_resolution != 0U) {
        planet.patch_resolution = config.planet.patch_resolution;
    }
    if (config.planet.max_lod_level_set) {
        planet.max_lod_level = config.planet.max_lod_level;
    }
    if (run_config_float_is_set(config.planet.lod_target_edge_px)) {
        planet.lod_target_edge_px = config.planet.lod_target_edge_px;
    }
    if (run_config_float_is_set(config.planet.lod_hysteresis)) {
        planet.lod_hysteresis = config.planet.lod_hysteresis;
    }
    if (config.planet.local_detail_lod_levels != 0U) {
        planet.local_detail_lod_levels = config.planet.local_detail_lod_levels;
    }
    if (config.planet.local_detail_cells_per_axis != 0U) {
        planet.local_detail_cells_per_axis = config.planet.local_detail_cells_per_axis;
    }
    if (run_config_float_is_set(config.planet.local_detail_outer_half_extent_m)) {
        planet.local_detail_outer_half_extent_m = config.planet.local_detail_outer_half_extent_m;
    }
    if (config.planet.local_detail_enabled >= 0) {
        planet.local_detail_enabled = config.planet.local_detail_enabled != 0;
    }
    if (run_config_float_is_set(config.planet.local_detail_height_strength_m)) {
        planet.local_detail_height_strength_m = config.planet.local_detail_height_strength_m;
    }
    if (run_config_float_is_set(config.planet.local_detail_scale_m)) {
        planet.local_detail_scale_m = config.planet.local_detail_scale_m;
    }
    if (config.planet.wire_overlay >= 0) {
        planet.wire_overlay = config.planet.wire_overlay != 0;
    }
    if (config.planet.skirts_enabled >= 0) {
        planet.skirts_enabled = config.planet.skirts_enabled != 0;
    }
    if (run_config_float_is_set(config.planet.skirt_depth_scale)) {
        planet.skirt_depth_scale = config.planet.skirt_depth_scale;
    }
    if (config.planet.terrain_enabled >= 0) {
        planet.terrain_enabled = config.planet.terrain_enabled != 0;
    }
    if (run_config_float_is_set(config.planet.terrain_height_scale_m)) {
        planet.terrain_height_scale_m = config.planet.terrain_height_scale_m;
    }
    if (run_config_float_is_set(config.planet.terrain_noise_scale)) {
        planet.terrain_noise_scale = config.planet.terrain_noise_scale;
    }
    if (run_config_float_is_set(config.planet.terrain_mid_detail_strength)) {
        planet.terrain_mid_detail_strength = config.planet.terrain_mid_detail_strength;
    }
    if (run_config_float_is_set(config.planet.terrain_fine_detail_strength)) {
        planet.terrain_fine_detail_strength = config.planet.terrain_fine_detail_strength;
    }
    if (run_config_float_is_set(config.planet.terrain_fine_detail_scale)) {
        planet.terrain_fine_detail_scale = config.planet.terrain_fine_detail_scale;
    }
    if (config.planet.terrain_seed_set) {
        planet.terrain_seed = config.planet.terrain_seed;
    }
    if (run_config_float_is_set(config.planet.sea_level_m)) {
        planet.sea_level_m = config.planet.sea_level_m;
    }
    if (run_config_float_is_set(config.planet.bathymetry_depth_scale_m)) {
        planet.bathymetry_depth_scale_m = config.planet.bathymetry_depth_scale_m;
    }
    if (run_config_float_is_set(config.planet.shoreline_width_m)) {
        planet.shoreline_width_m = config.planet.shoreline_width_m;
    }
    if (run_config_float_is_set(config.planet.atmosphere_haze_strength)) {
        planet.atmosphere_haze_strength = config.planet.atmosphere_haze_strength;
    }
    if (run_config_float_is_set(config.planet.atmosphere_haze_start)) {
        planet.atmosphere_haze_start = config.planet.atmosphere_haze_start;
    }
    if (run_config_float_is_set(config.planet.atmosphere_haze_end)) {
        planet.atmosphere_haze_end = config.planet.atmosphere_haze_end;
    }
    if (run_config_float_is_set(config.planet.atmosphere_aerial_strength)) {
        planet.atmosphere_aerial_strength = config.planet.atmosphere_aerial_strength;
    }
    if (!config.planet.atmosphere_mode.empty()) {
        planet.atmosphere_mode = planet_atmosphere_mode_from_string(config.planet.atmosphere_mode);
    }
    if (!config.debug_view.empty()) {
        planet.debug_view = planet_debug_view_from_string(config.debug_view);
    }
    validate_planet_config(planet);
    return planet;
}

PlanetConfigChangeKind planet_config_change_kind(const PlanetConfig& current,
                                                 const PlanetConfig& next) {
    if (current == next) {
        return PlanetConfigChangeKind::None;
    }
    if (current.patch_resolution != next.patch_resolution ||
        current.skirts_enabled != next.skirts_enabled) {
        return PlanetConfigChangeKind::SurfaceTopology;
    }
    if (current.local_detail_lod_levels != next.local_detail_lod_levels ||
        current.local_detail_cells_per_axis != next.local_detail_cells_per_axis ||
        current.local_detail_outer_half_extent_m != next.local_detail_outer_half_extent_m) {
        return PlanetConfigChangeKind::LocalDetailTopology;
    }
    return PlanetConfigChangeKind::Dynamic;
}

PlanetScalePreset planet_scale_preset_from_string(std::string_view value) {
    if (value.empty() || value == "earthlike" || value == "earth-like" || value == "earth") {
        return PlanetScalePreset::Earthlike;
    }
    if (value == "mini" || value == "debug") {
        return PlanetScalePreset::Mini;
    }
    throw std::runtime_error("unknown planet scale preset: " + std::string(value));
}

const char* planet_scale_preset_name(PlanetScalePreset preset) {
    switch (preset) {
    case PlanetScalePreset::Earthlike:
        return "earthlike";
    case PlanetScalePreset::Mini:
        return "mini";
    }
    return "earthlike";
}

PlanetDebugView planet_debug_view_from_string(std::string_view value) {
    if (value.empty() || value == "final") {
        return PlanetDebugView::Final;
    }
    if (value == "face" || value == "face-id" || value == "face_id") {
        return PlanetDebugView::FaceId;
    }
    if (value == "patch" || value == "patch-id" || value == "patch_id") {
        return PlanetDebugView::PatchId;
    }
    if (value == "lod" || value == "lod-level" || value == "lod_level") {
        return PlanetDebugView::LodLevel;
    }
    if (value == "screen-error" || value == "screen_error") {
        return PlanetDebugView::ScreenError;
    }
    if (value == "lod-transition" || value == "lod_transition" || value == "transition") {
        return PlanetDebugView::LodTransition;
    }
    if (value == "seam" || value == "seams") {
        return PlanetDebugView::Seams;
    }
    if (value == "cell-edge" || value == "cell_edge" || value == "cell") {
        return PlanetDebugView::CellEdge;
    }
    if (value == "terrain-height" || value == "terrain_height" || value == "height") {
        return PlanetDebugView::TerrainHeight;
    }
    if (value == "terrain-slope" || value == "terrain_slope" || value == "slope") {
        return PlanetDebugView::TerrainSlope;
    }
    if (value == "terrain-material" || value == "terrain_material" || value == "material") {
        return PlanetDebugView::TerrainMaterial;
    }
    if (value == "bathymetry" || value == "water-depth" || value == "water_depth") {
        return PlanetDebugView::Bathymetry;
    }
    if (value == "shoreline" || value == "shore") {
        return PlanetDebugView::Shoreline;
    }
    if (value == "land-mask" || value == "land_mask" || value == "land") {
        return PlanetDebugView::LandMask;
    }
    if (value == "moisture" || value == "terrain-moisture" || value == "terrain_moisture") {
        return PlanetDebugView::Moisture;
    }
    if (value == "temperature" || value == "terrain-temperature" ||
        value == "terrain_temperature") {
        return PlanetDebugView::Temperature;
    }
    if (value == "roughness" || value == "terrain-roughness" || value == "terrain_roughness") {
        return PlanetDebugView::Roughness;
    }
    if (value == "wireframe" || value == "wire" || value == "mesh") {
        return PlanetDebugView::Wireframe;
    }
    if (value == "celestial-planes" || value == "celestial_planes" || value == "celestial") {
        return PlanetDebugView::CelestialPlanes;
    }
    if (value == "local-detail-wireframe" || value == "local_detail_wireframe" ||
        value == "local-wireframe" || value == "local_wireframe") {
        return PlanetDebugView::LocalDetailWireframe;
    }
    if (value == "local-detail-blend" || value == "local_detail_blend" || value == "local-blend" ||
        value == "local_blend") {
        return PlanetDebugView::LocalDetailBlend;
    }
    if (value == "local-detail-height" || value == "local_detail_height" ||
        value == "local-height" || value == "local_height") {
        return PlanetDebugView::LocalDetailHeight;
    }
    throw std::runtime_error("unsupported planet debug view: " + std::string(value));
}

const char* planet_debug_view_name(PlanetDebugView view) {
    switch (view) {
    case PlanetDebugView::Final:
        return "final";
    case PlanetDebugView::FaceId:
        return "face-id";
    case PlanetDebugView::PatchId:
        return "patch-id";
    case PlanetDebugView::LodLevel:
        return "lod-level";
    case PlanetDebugView::ScreenError:
        return "screen-error";
    case PlanetDebugView::LodTransition:
        return "lod-transition";
    case PlanetDebugView::Seams:
        return "seams";
    case PlanetDebugView::CellEdge:
        return "cell-edge";
    case PlanetDebugView::TerrainHeight:
        return "terrain-height";
    case PlanetDebugView::TerrainSlope:
        return "terrain-slope";
    case PlanetDebugView::TerrainMaterial:
        return "terrain-material";
    case PlanetDebugView::Bathymetry:
        return "bathymetry";
    case PlanetDebugView::Shoreline:
        return "shoreline";
    case PlanetDebugView::LandMask:
        return "land-mask";
    case PlanetDebugView::Moisture:
        return "moisture";
    case PlanetDebugView::Temperature:
        return "temperature";
    case PlanetDebugView::Roughness:
        return "roughness";
    case PlanetDebugView::Wireframe:
        return "wireframe";
    case PlanetDebugView::CelestialPlanes:
        return "celestial-planes";
    case PlanetDebugView::LocalDetailWireframe:
        return "local-detail-wireframe";
    case PlanetDebugView::LocalDetailBlend:
        return "local-detail-blend";
    case PlanetDebugView::LocalDetailHeight:
        return "local-detail-height";
    }
    return "final";
}

bool planet_debug_view_is_local_detail(PlanetDebugView view) {
    return view == PlanetDebugView::LocalDetailWireframe ||
           view == PlanetDebugView::LocalDetailBlend || view == PlanetDebugView::LocalDetailHeight;
}

PlanetAtmosphereMode planet_atmosphere_mode_from_string(std::string_view value) {
    if (value.empty() || value == "analytic") {
        return PlanetAtmosphereMode::Analytic;
    }
    if (value == "physical" || value == "physical-preview" || value == "physical_preview") {
        return PlanetAtmosphereMode::Physical;
    }
    throw std::runtime_error("unsupported planet atmosphere mode: " + std::string(value));
}

const char* planet_atmosphere_mode_name(PlanetAtmosphereMode mode) {
    switch (mode) {
    case PlanetAtmosphereMode::Analytic:
        return "analytic";
    case PlanetAtmosphereMode::Physical:
        return "physical";
    }
    return "analytic";
}

void validate_planet_config(const PlanetConfig& config) {
    if (config.radius_m <= 0.0F) {
        throw std::runtime_error("planet radius must be positive");
    }
    if (config.atmosphere_height_m < 0.0F) {
        throw std::runtime_error("planet atmosphere height must be nonnegative");
    }
    if (config.camera_altitude_m < 0.0F) {
        throw std::runtime_error("planet camera altitude must be nonnegative");
    }
    if (config.patches_per_face == 0U) {
        throw std::runtime_error("planet patches per face must be positive");
    }
    if (config.patch_resolution == 0U) {
        throw std::runtime_error("planet patch resolution must be positive");
    }
    if (config.max_lod_level > kPlanetMaxLiveLodLevel) {
        throw std::runtime_error("planet max LOD level exceeds live cap");
    }
    if (config.patch_resolution > kPlanetMaxPatchResolution) {
        throw std::runtime_error("planet patch resolution must be <= 128");
    }
    if (config.patches_per_face > 8U) {
        throw std::runtime_error("planet patches per face must be <= 8");
    }
    if (config.lod_target_edge_px <= 0.0F) {
        throw std::runtime_error("planet LOD target edge pixels must be positive");
    }
    if (!std::isfinite(config.lod_hysteresis) || config.lod_hysteresis < 0.0F ||
        config.lod_hysteresis >= 1.0F) {
        throw std::runtime_error("planet LOD hysteresis must be finite and in [0, 1)");
    }
    if (config.local_detail_lod_levels == 0U ||
        config.local_detail_lod_levels > kPlanetMaxLocalDetailLodLevels) {
        throw std::runtime_error("planet local detail LOD levels out of supported range");
    }
    if (config.local_detail_cells_per_axis == 0U ||
        config.local_detail_cells_per_axis > kPlanetMaxLocalDetailCellsPerAxis) {
        throw std::runtime_error("planet local detail cells per axis out of supported range");
    }
    if (!std::isfinite(config.local_detail_outer_half_extent_m) ||
        config.local_detail_outer_half_extent_m <= 0.0F) {
        throw std::runtime_error("planet local detail outer extent must be finite and positive");
    }
    if (!std::isfinite(config.local_detail_height_strength_m) ||
        config.local_detail_height_strength_m < 0.0F) {
        throw std::runtime_error(
            "planet local detail height strength must be finite and nonnegative");
    }
    if (!std::isfinite(config.local_detail_scale_m) || config.local_detail_scale_m <= 0.0F) {
        throw std::runtime_error("planet local detail scale must be finite and positive");
    }
    if (config.skirt_depth_scale <= 0.0F) {
        throw std::runtime_error("planet skirt depth scale must be positive");
    }
    if (!std::isfinite(config.terrain_height_scale_m) || config.terrain_height_scale_m < 0.0F) {
        throw std::runtime_error("planet terrain height scale must be finite and nonnegative");
    }
    if (!std::isfinite(config.terrain_noise_scale) || config.terrain_noise_scale <= 0.0F) {
        throw std::runtime_error("planet terrain noise scale must be finite and positive");
    }
    if (!std::isfinite(config.terrain_mid_detail_strength) ||
        config.terrain_mid_detail_strength < 0.0F) {
        throw std::runtime_error(
            "planet terrain mid-detail strength must be finite and nonnegative");
    }
    if (!std::isfinite(config.terrain_fine_detail_strength) ||
        config.terrain_fine_detail_strength < 0.0F) {
        throw std::runtime_error(
            "planet terrain fine-detail strength must be finite and nonnegative");
    }
    if (!std::isfinite(config.terrain_fine_detail_scale) ||
        config.terrain_fine_detail_scale <= 0.0F) {
        throw std::runtime_error("planet terrain fine-detail scale must be finite and positive");
    }
    if (!std::isfinite(config.sea_level_m)) {
        throw std::runtime_error("planet sea level must be finite");
    }
    if (!std::isfinite(config.bathymetry_depth_scale_m) ||
        config.bathymetry_depth_scale_m <= 0.0F) {
        throw std::runtime_error("planet bathymetry depth scale must be finite and positive");
    }
    if (!std::isfinite(config.shoreline_width_m) || config.shoreline_width_m <= 0.0F) {
        throw std::runtime_error("planet shoreline width must be finite and positive");
    }
    if (!std::isfinite(config.atmosphere_haze_strength) || config.atmosphere_haze_strength < 0.0F ||
        config.atmosphere_haze_strength > 1.0F) {
        throw std::runtime_error("planet atmosphere haze strength must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.atmosphere_haze_start) || config.atmosphere_haze_start < 0.0F ||
        config.atmosphere_haze_start > 1.0F) {
        throw std::runtime_error("planet atmosphere haze start must be finite and in [0, 1]");
    }
    if (!std::isfinite(config.atmosphere_haze_end) ||
        config.atmosphere_haze_end < config.atmosphere_haze_start ||
        config.atmosphere_haze_end > 1.5F) {
        throw std::runtime_error("planet atmosphere haze end must be finite, >= start, and <= 1.5");
    }
    if (!std::isfinite(config.atmosphere_aerial_strength) ||
        config.atmosphere_aerial_strength < 0.0F || config.atmosphere_aerial_strength > 1.0F) {
        throw std::runtime_error("planet atmosphere aerial strength must be finite and in [0, 1]");
    }
}

} // namespace cubey::projects::planet
