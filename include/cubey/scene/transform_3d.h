#pragma once

#include <cubey/core/math.h>

namespace cubey {

struct Transform3D {
    math::Vec3 translation{0.0F, 0.0F, 0.0F};
    math::Quat rotation{math::identity_quat()};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] math::Mat4 affine_matrix() const {
        return math::translation(translation) * math::rotation(rotation) * math::scale(scale);
    }
};

} // namespace cubey
