#pragma once

#include <cubey/core/run_config.h>

namespace cubey::projects::planet {

inline constexpr float kPlanetDefaultRadiusM = 600000.0F;
inline constexpr float kPlanetDefaultAtmosphereHeightM = 70000.0F;
inline constexpr float kPlanetDefaultCameraAltitudeM = 240000.0F;

struct PlanetConfig {
    float radius_m = kPlanetDefaultRadiusM;
    float atmosphere_height_m = kPlanetDefaultAtmosphereHeightM;
    float camera_altitude_m = kPlanetDefaultCameraAltitudeM;
};

[[nodiscard]] PlanetConfig planet_config_from_run_config(const RunConfig& config);
void validate_planet_config(const PlanetConfig& config);

} // namespace cubey::projects::planet
