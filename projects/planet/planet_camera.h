#pragma once

#include "planet_config.h"

#include <cubey/scene/transform_3d.h>

namespace cubey::projects::planet {

struct PlanetCameraState {
    cubey::math::Vec3 position_m{0.0F, 0.0F, kPlanetDefaultRadiusM + kPlanetDefaultCameraAltitudeM};
    cubey::math::Quat surface_rotation{cubey::math::identity_quat()};
    bool surface_rotation_active = false;
};

[[nodiscard]] float planet_camera_min_altitude_m(const PlanetConfig& config);
[[nodiscard]] float planet_surface_camera_blend(const PlanetConfig& config, float distance_m);
[[nodiscard]] float planet_camera_home_distance_m(const PlanetConfig& config);
[[nodiscard]] float planet_camera_min_distance_m(const PlanetConfig& config);
[[nodiscard]] float planet_camera_max_distance_m(const PlanetConfig& config);
[[nodiscard]] float planet_camera_distance_m(const PlanetCameraState& state);
[[nodiscard]] PlanetCameraState planet_camera_home_state(const PlanetConfig& config,
                                                         float base_yaw_radians,
                                                         float base_pitch_radians);
void planet_camera_set_distance(PlanetCameraState& state, const PlanetConfig& config,
                                float distance_m);
void planet_camera_zoom_by_scroll(PlanetCameraState& state, const PlanetConfig& config,
                                  double scroll_y);
void planet_camera_orbit_drag(PlanetCameraState& state, const PlanetConfig& config,
                              double delta_x_px, double delta_y_px);
void planet_camera_surface_look_drag(PlanetCameraState& state, const PlanetConfig& config,
                                     double delta_x_px, double delta_y_px);
[[nodiscard]] bool planet_camera_surface_move(PlanetCameraState& state, const PlanetConfig& config,
                                              float forward, float right, double delta_seconds);
void planet_camera_update_surface_mode(PlanetCameraState& state, const PlanetConfig& config);
void planet_camera_reset_surface_view(PlanetCameraState& state, const PlanetConfig& config);
[[nodiscard]] cubey::Transform3D make_planet_camera_transform(const PlanetConfig& config,
                                                              PlanetCameraState state);

} // namespace cubey::projects::planet
