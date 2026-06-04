#include "planet_config.h"

#include <limits>
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
    const std::uint64_t vertices = 6ULL * static_cast<std::uint64_t>(config.patches_per_face) *
                                   static_cast<std::uint64_t>(config.patches_per_face) *
                                   static_cast<std::uint64_t>(config.patch_resolution + 1U) *
                                   static_cast<std::uint64_t>(config.patch_resolution + 1U);
    if (vertices > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max())) {
        throw std::runtime_error("planet surface exceeds uint16 mesh index limit");
    }
}

} // namespace cubey::projects::planet
