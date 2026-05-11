#pragma once

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace cubey::math {

using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;

[[nodiscard]] inline Mat4 identity() {
    return Mat4{1.0F};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline Mat4 translation(float x, float y, float z) {
    return glm::translate(identity(), glm::vec3{x, y, z});
}

[[nodiscard]] inline Mat4 translation(Vec3 offset) {
    return glm::translate(identity(), offset);
}

[[nodiscard]] inline Mat4 rotation_x(float radians) {
    return glm::rotate(identity(), radians, glm::vec3{1.0F, 0.0F, 0.0F});
}

[[nodiscard]] inline Mat4 rotation_y(float radians) {
    return glm::rotate(identity(), radians, glm::vec3{0.0F, 1.0F, 0.0F});
}

[[nodiscard]] inline Mat4 rotation_z(float radians) {
    return glm::rotate(identity(), radians, glm::vec3{0.0F, 0.0F, 1.0F});
}

[[nodiscard]] inline Quat identity_quat() {
    return Quat{1.0F, 0.0F, 0.0F, 0.0F};
}

[[nodiscard]] inline Quat angle_axis_quat(float radians, Vec3 axis) {
    if (glm::length(axis) == 0.0F) {
        return identity_quat();
    }
    return glm::angleAxis(radians, glm::normalize(axis));
}

[[nodiscard]] inline Quat euler_xyz_quat(Vec3 radians) {
    return angle_axis_quat(radians.z, {0.0F, 0.0F, 1.0F}) *
           angle_axis_quat(radians.y, {0.0F, 1.0F, 0.0F}) *
           angle_axis_quat(radians.x, {1.0F, 0.0F, 0.0F});
}

[[nodiscard]] inline Mat4 rotation(Quat quaternion) {
    return glm::mat4_cast(glm::normalize(quaternion));
}

[[nodiscard]] inline Mat4 scale(Vec3 scale) {
    return glm::scale(identity(), scale);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z) {
    Mat4 projection = glm::perspective(fovy_radians, aspect, near_z, far_z);
    projection[1][1] *= -1.0F;
    return projection;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline Mat4 orthographic(float left, float right, float bottom, float top,
                                       float near_z, float far_z) {
    Mat4 projection = glm::ortho(left, right, bottom, top, near_z, far_z);
    projection[1][1] *= -1.0F;
    return projection;
}

} // namespace cubey::math
