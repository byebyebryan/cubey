#pragma once

#include "terrain_source.h"

#include <cubey/scene/transform_3d.h>

namespace cubey::projects::terrain {

struct TerrainBackdropCameraPlan {
    cubey::Transform3D transform{};
    cubey::math::Vec2 anchor_xz{0.0F, 0.0F};
    cubey::math::Vec3 target_position{0.0F, 0.0F, 0.0F};
    float yaw_radians = 0.0F;
    float pitch_radians = 0.0F;
    float target_distance_m = 0.0F;
    float target_elevation_radians = 0.0F;
    float score = 0.0F;
};

[[nodiscard]] TerrainBackdropCameraPlan
plan_terrain_backdrop_camera(const TerrainSourceParameters& source, float vertical_scale = 1.0F);

} // namespace cubey::projects::terrain
