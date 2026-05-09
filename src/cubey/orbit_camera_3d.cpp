#include <cubey/orbit_camera_3d.h>

#include <cubey/orbit_controller.h>

#include <algorithm>

namespace cubey {

namespace {

constexpr float kMinimumDistance = 0.000001F;
constexpr float kMinimumNearZ = 0.000001F;

} // namespace

OrbitCamera3D::OrbitCamera3D(OrbitCamera3DConfig config) : config_(config) {
    reset();
}

void OrbitCamera3D::reset() {
    target_ = config_.target;
    distance_ = std::max(config_.distance, kMinimumDistance);
    yaw_ = config_.yaw;
    pitch_ = config_.pitch;
    fovy_radians_ = config_.fovy_radians;
    near_z_ = std::max(config_.near_z, kMinimumNearZ);
    far_z_ = std::max(config_.far_z, near_z_ + kMinimumNearZ);
}

void OrbitCamera3D::set_target(math::Vec3 target) {
    target_ = target;
}

void OrbitCamera3D::set_distance(float distance) {
    distance_ = std::max(distance, kMinimumDistance);
}

void OrbitCamera3D::set_orbit(float yaw, float pitch) {
    yaw_ = yaw;
    pitch_ = pitch;
}

void OrbitCamera3D::set_orbit(const OrbitController& controller) {
    set_orbit(controller.yaw(), controller.pitch());
}

void OrbitCamera3D::set_projection(float fovy_radians, float near_z, float far_z) {
    fovy_radians_ = fovy_radians;
    near_z_ = std::max(near_z, kMinimumNearZ);
    far_z_ = std::max(far_z, near_z_ + kMinimumNearZ);
}

math::Mat4 OrbitCamera3D::view_matrix() const {
    return math::translation(0.0F, 0.0F, -distance_) * math::rotation_x(-pitch_) *
           math::rotation_y(-yaw_) * math::translation(-target_);
}

math::Mat4 OrbitCamera3D::projection_matrix(float aspect) const {
    return math::perspective(fovy_radians_, aspect, near_z_, far_z_);
}

math::Mat4 OrbitCamera3D::view_projection_matrix(float aspect) const {
    return projection_matrix(aspect) * view_matrix();
}

} // namespace cubey
