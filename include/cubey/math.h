#pragma once

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace cubey::math {

using Mat4 = glm::mat4;
using Vec4 = glm::vec4;

[[nodiscard]] inline Mat4 identity() {
    return Mat4{1.0F};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline Mat4 translation(float x, float y, float z) {
    return glm::translate(identity(), glm::vec3{x, y, z});
}

[[nodiscard]] inline Mat4 rotation_x(float radians) {
    return glm::rotate(identity(), radians, glm::vec3{1.0F, 0.0F, 0.0F});
}

[[nodiscard]] inline Mat4 rotation_y(float radians) {
    return glm::rotate(identity(), radians, glm::vec3{0.0F, 1.0F, 0.0F});
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z) {
    Mat4 projection = glm::perspective(fovy_radians, aspect, near_z, far_z);
    projection[1][1] *= -1.0F;
    return projection;
}

} // namespace cubey::math
