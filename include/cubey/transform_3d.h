#pragma once

#include <cubey/math.h>

namespace cubey {

struct Transform3D {
    math::Vec3 position{0.0F, 0.0F, 0.0F};
    math::Vec3 rotation_radians{0.0F, 0.0F, 0.0F};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] math::Mat4 model_matrix() const {
        return math::translation(position) * math::rotation_z(rotation_radians.z) *
               math::rotation_y(rotation_radians.y) * math::rotation_x(rotation_radians.x) *
               math::scale(scale);
    }
};

} // namespace cubey
