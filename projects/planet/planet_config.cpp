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
    if (value == "seam" || value == "seams") {
        return PlanetDebugView::Seams;
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
    case PlanetDebugView::Seams:
        return "seams";
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
        throw std::runtime_error("planet max LOD level must be <= 6");
    }
    if (config.patch_resolution > 32U) {
        throw std::runtime_error("planet patch resolution must be <= 32");
    }
    if (config.patches_per_face > 8U) {
        throw std::runtime_error("planet patches per face must be <= 8");
    }
    if (config.lod_target_edge_px <= 0.0F) {
        throw std::runtime_error("planet LOD target edge pixels must be positive");
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
}

} // namespace cubey::projects::planet
