#pragma once

#include <cubey/core/math.h>

#include <cmath>
#include <stdexcept>

namespace cubey::render {

struct ViewRayBasis3D {
    math::Vec4 right_aspect{1.0F, 0.0F, 0.0F, 1.0F};
    math::Vec4 up_tan_half_fovy{0.0F, 1.0F, 0.0F, 1.0F};
    math::Vec4 forward{0.0F, 0.0F, -1.0F, 0.0F};
};

[[nodiscard]] inline ViewRayBasis3D view_ray_basis_3d(math::Quat camera_rotation, float aspect,
                                                      float fovy_radians) {
    if (!std::isfinite(aspect) || aspect <= 0.0F) {
        throw std::runtime_error("view ray basis aspect must be positive");
    }
    if (!std::isfinite(fovy_radians) || fovy_radians <= 0.0F) {
        throw std::runtime_error("view ray basis fovy must be positive");
    }

    const math::Quat rotation = glm::normalize(camera_rotation);
    const float tan_half_fovy = std::tan(fovy_radians * 0.5F);
    const math::Vec3 right = glm::normalize(rotation * math::Vec3{1.0F, 0.0F, 0.0F});
    const math::Vec3 up = glm::normalize(rotation * math::Vec3{0.0F, 1.0F, 0.0F});
    const math::Vec3 forward = glm::normalize(rotation * math::Vec3{0.0F, 0.0F, -1.0F});

    return {
        .right_aspect = {right.x, right.y, right.z, aspect},
        .up_tan_half_fovy = {up.x, up.y, up.z, tan_half_fovy},
        .forward = {forward.x, forward.y, forward.z, 0.0F},
    };
}

[[nodiscard]] inline math::Vec3 view_ray_direction(const ViewRayBasis3D& basis,
                                                   math::Vec2 ndc) {
    const float tan_half_fovy = basis.up_tan_half_fovy.w;
    return glm::normalize(math::Vec3{basis.forward} +
                          math::Vec3{basis.right_aspect} * ndc.x * basis.right_aspect.w *
                              tan_half_fovy -
                          math::Vec3{basis.up_tan_half_fovy} * ndc.y * tan_half_fovy);
}

} // namespace cubey::render
