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
        .focus_distance = camera.focus_distance,
    };
}

ReferenceCamera orbit_reference_camera(const ReferenceCamera& camera, float yaw_radians,
                                       float pitch_radians, float distance) {
    if (!std::isfinite(yaw_radians) || !std::isfinite(pitch_radians) || !std::isfinite(distance) ||
        distance <= 0.0F || camera.focus_distance <= 0.0F) {
        throw std::runtime_error("reference orbit values must be finite and positive");
    }
    if (yaw_radians == 0.0F && pitch_radians == 0.0F && distance == camera.focus_distance) {
        return camera;
    }

    const cubey::math::Quat yaw = cubey::math::angle_axis_quat(yaw_radians, {0.0F, 1.0F, 0.0F});
    const cubey::math::Vec3 yawed_right = glm::normalize(yaw * camera.right);
    const cubey::math::Quat pitch = cubey::math::angle_axis_quat(pitch_radians, yawed_right);
    const cubey::math::Quat rotation = pitch * yaw;
    const cubey::math::Vec3 forward = glm::normalize(rotation * camera.forward);
    const cubey::math::Vec3 target = camera.position + camera.forward * camera.focus_distance;
    return {
        .position = target - forward * distance,
        .right = glm::normalize(rotation * camera.right),
        .up = glm::normalize(rotation * camera.up),
        .forward = forward,
        .focus_distance = camera.focus_distance,
    };
}

} // namespace cubey::projects::terrain_shadertoy_ref
