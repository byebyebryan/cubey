#include "planet_config.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace cubey::projects::planet {

PlanetConfig planet_config_from_run_config(const RunConfig& config) {
    PlanetConfig planet{};
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
    if (!config.debug_view.empty()) {
        planet.debug_view = planet_debug_view_from_string(config.debug_view);
    }
    validate_planet_config(planet);
    return planet;
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
    if (value == "wireframe" || value == "wire" || value == "mesh") {
        return PlanetDebugView::Wireframe;
    }
    if (value == "celestial-planes" || value == "celestial_planes" || value == "celestial") {
        return PlanetDebugView::CelestialPlanes;
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
    case PlanetDebugView::Wireframe:
        return "wireframe";
    case PlanetDebugView::CelestialPlanes:
        return "celestial-planes";
    }
    return "final";
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
        throw std::runtime_error("planet max LOD level must be <= 9");
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
}

} // namespace cubey::projects::planet
