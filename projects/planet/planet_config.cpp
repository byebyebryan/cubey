#include "planet_config.h"

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
    if (config.max_lod_level > 3U) {
        throw std::runtime_error("planet max LOD level must be <= 3");
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
    std::uint64_t patch_multiplier = 1;
    for (std::uint32_t level = 0; level < config.max_lod_level; ++level) {
        patch_multiplier *= 4ULL;
    }
    const std::uint64_t worst_case_vertices =
        6ULL * static_cast<std::uint64_t>(config.patches_per_face) *
        static_cast<std::uint64_t>(config.patches_per_face) * patch_multiplier *
        (static_cast<std::uint64_t>(config.patch_resolution + 1U) *
             static_cast<std::uint64_t>(config.patch_resolution + 1U) +
         8ULL * static_cast<std::uint64_t>(config.patch_resolution));
    if (worst_case_vertices > 2000000ULL) {
        throw std::runtime_error("planet surface LOD settings are too dense for CPU debug mesh");
    }
}

} // namespace cubey::projects::planet
