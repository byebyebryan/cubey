#pragma once

#include "planet_config.h"

#include <cubey/core/math.h>
#include <cubey/render/local_tangent_frame.h>
#include <cubey/scene/transform_3d.h>

namespace cubey::projects::planet {

struct PlanetFrame {
    float planet_radius_m = kPlanetDefaultRadiusM;
    float atmosphere_outer_radius_m = kPlanetDefaultRadiusM + kPlanetDefaultAtmosphereHeightM;
    float camera_radius_m = kPlanetDefaultRadiusM + kPlanetDefaultCameraAltitudeM;
    float camera_altitude_m = kPlanetDefaultCameraAltitudeM;
    float horizon_distance_m = 0.0F;
    float near_plane_m = 1.0F;
    float far_plane_m = 1.0F;
    cubey::math::DVec3 camera_world_position_m{0.0, 0.0, 0.0};
    cubey::math::DVec3 surface_origin_m{0.0, 0.0, 0.0};
    cubey::render::LocalTangentFrame local_frame{};
};

[[nodiscard]] PlanetFrame make_planet_frame(const PlanetConfig& config,
                                            const cubey::Transform3D& camera_transform);

} // namespace cubey::projects::planet
