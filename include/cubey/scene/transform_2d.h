#pragma once

#include <cubey/core/math.h>

#include <cmath>

namespace cubey {

struct Transform2D {
    math::Vec2 translation{0.0F, 0.0F};
    float rotation_radians = 0.0F;
    math::Vec2 scale{1.0F, 1.0F};

    [[nodiscard]] math::Mat3 affine_matrix() const {
        const float cosine = std::cos(rotation_radians);
        const float sine = std::sin(rotation_radians);

        math::Mat3 matrix{1.0F};
        matrix[0][0] = cosine * scale.x;
        matrix[0][1] = sine * scale.x;
        matrix[1][0] = -sine * scale.y;
        matrix[1][1] = cosine * scale.y;
        matrix[2][0] = translation.x;
        matrix[2][1] = translation.y;
        return matrix;
    }
};

} // namespace cubey
