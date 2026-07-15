#include "terrain_shadertoy_ref_camera.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace cubey::projects::terrain_shadertoy_ref {

float normalize_reference_yaw_degrees(float yaw_degrees) {
    if (!std::isfinite(yaw_degrees)) {
        throw std::runtime_error("reference yaw must be finite");
    }
    const float normalized = std::remainder(yaw_degrees, 360.0F);
    return normalized == 0.0F ? 0.0F : normalized;
}

ReferenceCamera rotate_reference_camera_yaw(const ReferenceCamera& camera, float yaw_degrees) {
    const float normalized = normalize_reference_yaw_degrees(yaw_degrees);
    if (normalized == 0.0F) {
        return camera;
    }

    const float radians = normalized * std::numbers::pi_v<float> / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const auto rotate = [cosine, sine](cubey::math::Vec3 value) {
        return cubey::math::Vec3{cosine * value.x + sine * value.z, value.y,
                                 -sine * value.x + cosine * value.z};
    };
    return {
        .position = camera.position,
        .right = rotate(camera.right),
        .up = rotate(camera.up),
        .forward = rotate(camera.forward),
    };
}

} // namespace cubey::projects::terrain_shadertoy_ref
