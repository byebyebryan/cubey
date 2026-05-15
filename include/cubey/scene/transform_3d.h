#pragma once

#include <cubey/core/math.h>

#include <optional>

namespace cubey {

struct Transform3D {
    math::Vec3 translation{0.0F, 0.0F, 0.0F};
    math::Quat rotation{math::identity_quat()};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
    std::optional<math::Mat4> affine_override{};

    [[nodiscard]] static Transform3D from_affine_matrix(math::Mat4 matrix) {
        Transform3D transform;
        transform.affine_override = matrix;
        return transform;
    }

    [[nodiscard]] bool has_affine_matrix() const noexcept {
        return affine_override.has_value();
    }

    void clear_affine_matrix() noexcept {
        affine_override.reset();
    }

    [[nodiscard]] math::Mat4 affine_matrix() const {
        if (affine_override) {
            return *affine_override;
        }
        return math::translation(translation) * math::rotation(rotation) * math::scale(scale);
    }
};

} // namespace cubey
