#pragma once

#include "planet_config.h"

#include <cubey/scene/transform_3d.h>

namespace cubey::projects::planet {

struct PlanetCameraState {
    float distance_m = kPlanetDefaultRadiusM + kPlanetDefaultCameraAltitudeM;
    float yaw_radians = 0.0F;
    float pitch_radians = 0.0F;
    float surface_anchor_yaw_radians = 0.0F;
    float surface_anchor_pitch_radians = 0.0F;
    bool surface_anchor_active = false;
};

[[nodiscard]] float planet_camera_min_altitude_m(const PlanetConfig& config);
[[nodiscard]] float planet_surface_camera_blend(const PlanetConfig& config, float distance_m);
[[nodiscard]] cubey::Transform3D make_planet_camera_transform(const PlanetConfig& config,
                                                              PlanetCameraState state);

} // namespace cubey::projects::planet
