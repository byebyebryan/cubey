#pragma once

#include "ocean_horizon.h"

#include <cubey/render/local_tangent_frame.h>

namespace cubey::projects::ocean {

struct OceanSurfaceFrame {
    cubey::render::LocalTangentFrame local_frame{};
    cubey::math::DVec3 camera_world_position_m{0.0, 0.0, 0.0};
    cubey::math::Vec3 camera_local_position_m{0.0F, 0.0F, 0.0F};
    OceanConfig mesh_config{};
    OceanHorizonDiagnostics horizon{};
    float projection_far_plane_m = 1.0F;
    bool flat_surface = true;
};

[[nodiscard]] inline cubey::render::LocalTangentFrame
ocean_local_tangent_frame(float planet_radius_m, float water_datum_m = 0.0F) {
    cubey::render::LocalTangentFrame frame{};
    frame.planet_radius_m = planet_radius_m;
    frame.water_datum_m = water_datum_m;
    cubey::render::validate_local_tangent_frame(frame);
    return frame;
}

[[nodiscard]] inline OceanSurfaceFrame
ocean_surface_frame_from_camera(const OceanConfig& config,
                                cubey::math::Vec3 camera_position_m,
                                float planet_radius_m,
                                float water_datum_m = 0.0F) {
    OceanSurfaceFrame frame{};
    frame.local_frame = ocean_local_tangent_frame(planet_radius_m, water_datum_m);
    frame.camera_world_position_m = {
        static_cast<double>(camera_position_m.x),
        static_cast<double>(camera_position_m.y),
        static_cast<double>(camera_position_m.z),
    };
    frame.camera_local_position_m = cubey::render::local_tangent_world_to_local_m(
        frame.local_frame, frame.camera_world_position_m);
    frame.mesh_config = ocean_horizon_effective_mesh_config(
        config, frame.camera_local_position_m.y, frame.local_frame.water_datum_m,
        frame.local_frame.planet_radius_m);
    frame.horizon =
        ocean_horizon_diagnostics(frame.mesh_config, frame.camera_local_position_m.y,
                                  frame.local_frame.water_datum_m,
                                  frame.local_frame.planet_radius_m,
                                  config.horizon_extent_margin);
    frame.projection_far_plane_m = ocean_horizon_projection_far_plane_m(frame.horizon);
    return frame;
}

} // namespace cubey::projects::ocean
