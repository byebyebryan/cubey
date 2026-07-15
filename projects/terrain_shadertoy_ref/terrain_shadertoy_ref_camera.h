#pragma once

#include <cubey/core/math.h>

namespace cubey::projects::terrain_shadertoy_ref {

struct ReferenceCamera {
    cubey::math::Vec3 position{};
    cubey::math::Vec3 right{};
    cubey::math::Vec3 up{};
    cubey::math::Vec3 forward{};
};

[[nodiscard]] float normalize_reference_yaw_degrees(float yaw_degrees);
[[nodiscard]] ReferenceCamera rotate_reference_camera_yaw(const ReferenceCamera& camera,
                                                           float yaw_degrees);

} // namespace cubey::projects::terrain_shadertoy_ref
