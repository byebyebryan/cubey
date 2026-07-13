#pragma once

#include "terrain_source.h"

#include <cubey/scene/transform_3d.h>

#include <cstdint>
#include <string_view>

namespace cubey::projects::terrain {

enum class TerrainBackdropCameraProfile : std::uint8_t {
    Backdrop,
    Midground,
};

[[nodiscard]] std::string_view
terrain_backdrop_camera_profile_name(TerrainBackdropCameraProfile profile) noexcept;

struct TerrainBackdropCameraPlan {
    cubey::Transform3D transform{};
    cubey::math::Vec2 anchor_xz{0.0F, 0.0F};
    cubey::math::Vec3 target_position{0.0F, 0.0F, 0.0F};
    float yaw_radians = 0.0F;
    float pitch_radians = 0.0F;
    float target_distance_m = 0.0F;
    float target_elevation_radians = 0.0F;
    float camera_clearance_m = 0.0F;
    float clearance_raise_m = 0.0F;
    float foreground_clear_distance_m = 0.0F;
    float foreground_min_margin_m = 0.0F;
    float aspect_ratio = 16.0F / 9.0F;
    float score = 0.0F;
};

[[nodiscard]] TerrainBackdropCameraPlan plan_terrain_backdrop_camera(
    const TerrainSourceParameters& source, float vertical_scale = 1.0F,
    float aspect_ratio = 16.0F / 9.0F,
    TerrainBackdropCameraProfile profile = TerrainBackdropCameraProfile::Backdrop);

} // namespace cubey::projects::terrain
