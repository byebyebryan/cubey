#include <cubey/scene/camera_3d.h>

#include <algorithm>
#include <glm/glm.hpp>

namespace cubey {

namespace {

constexpr float kMinimumDistance = 0.000001F;
constexpr float kMinimumNearZ = 0.000001F;
constexpr float kMinimumOrthographicHeight = 0.000001F;

} // namespace

Camera3D::Camera3D(Camera3DConfig config) : config_(config) {
    reset();
}

void Camera3D::reset() {
    projection_ = config_.projection;
    fovy_radians_ = config_.fovy_radians;
    orthographic_height_ = std::max(config_.orthographic_height, kMinimumOrthographicHeight);
    near_z_ = std::max(config_.near_z, kMinimumNearZ);
    far_z_ = std::max(config_.far_z, near_z_ + kMinimumNearZ);
}

void Camera3D::set_projection(float fovy_radians, float near_z, float far_z) {
    projection_ = Camera3DProjection::Perspective;
    fovy_radians_ = fovy_radians;
    near_z_ = std::max(near_z, kMinimumNearZ);
    far_z_ = std::max(far_z, near_z_ + kMinimumNearZ);
}

void Camera3D::set_orthographic(float orthographic_height, float near_z, float far_z) {
    projection_ = Camera3DProjection::Orthographic;
    orthographic_height_ = std::max(orthographic_height, kMinimumOrthographicHeight);
    near_z_ = std::max(near_z, kMinimumNearZ);
    far_z_ = std::max(far_z, near_z_ + kMinimumNearZ);
}

math::Mat4 Camera3D::projection_matrix(float aspect) const {
    if (projection_ == Camera3DProjection::Orthographic) {
        const float half_height = orthographic_height_ * 0.5F;
        const float half_width = half_height * aspect;
        return math::orthographic(-half_width, half_width, -half_height, half_height, near_z_,
                                  far_z_);
    }
    return math::perspective(fovy_radians_, aspect, near_z_, far_z_);
}

math::Mat4 Camera3D::view_matrix(const Transform3D& camera_world_transform) const {
    return view_matrix(camera_world_transform.affine_matrix());
}

math::Mat4 Camera3D::view_matrix(const math::Mat4& camera_world_matrix) const {
    return glm::inverse(camera_world_matrix);
}

math::Mat4 Camera3D::view_projection_matrix(const Transform3D& camera_world_transform,
                                            float aspect) const {
    return view_projection_matrix(camera_world_transform.affine_matrix(), aspect);
}

math::Mat4 Camera3D::view_projection_matrix(const math::Mat4& camera_world_matrix,
                                            float aspect) const {
    return projection_matrix(aspect) * view_matrix(camera_world_matrix);
}

Transform3D orbit_camera_transform(const OrbitCameraState& state) {
    const float distance = std::max(state.distance, kMinimumDistance);
    const math::Quat rotation = math::angle_axis_quat(state.yaw, {0.0F, 1.0F, 0.0F}) *
                                math::angle_axis_quat(state.pitch, {1.0F, 0.0F, 0.0F});
    const math::Vec3 offset = rotation * math::Vec3{0.0F, 0.0F, distance};
    return Transform3D{
        .translation = state.target + offset,
        .rotation = rotation,
    };
}

} // namespace cubey
