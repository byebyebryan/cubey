#pragma once

#include <cubey/core/run_config.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::planet {

inline constexpr float kPlanetDefaultRadiusM = 600000.0F;
inline constexpr float kPlanetDefaultAtmosphereHeightM = 70000.0F;
inline constexpr float kPlanetDefaultCameraAltitudeM = 240000.0F;
inline constexpr std::uint32_t kPlanetDefaultPatchesPerFace = 2;
inline constexpr std::uint32_t kPlanetDefaultPatchResolution = 16;

enum class PlanetDebugView : std::uint8_t {
    Final,
    FaceId,
    PatchId,
};

struct PlanetConfig {
    float radius_m = kPlanetDefaultRadiusM;
    float atmosphere_height_m = kPlanetDefaultAtmosphereHeightM;
    float camera_altitude_m = kPlanetDefaultCameraAltitudeM;
    std::uint32_t patches_per_face = kPlanetDefaultPatchesPerFace;
    std::uint32_t patch_resolution = kPlanetDefaultPatchResolution;
    PlanetDebugView debug_view = PlanetDebugView::Final;
    bool wire_overlay = false;
};

[[nodiscard]] PlanetConfig planet_config_from_run_config(const RunConfig& config);
[[nodiscard]] PlanetDebugView planet_debug_view_from_string(std::string_view value);
[[nodiscard]] const char* planet_debug_view_name(PlanetDebugView view);
void validate_planet_config(const PlanetConfig& config);

} // namespace cubey::projects::planet
