#pragma once

#include <cubey/camera_2d.h>

#include <cstdint>

namespace cubey::examples::fractal {

struct FractalPushConstants {
    float center_x = -0.5F;
    float center_y = 0.0F;
    float scale = 1.35F;
    float aspect = 1.0F;
    std::int32_t max_iterations = 180;
};

class FractalView {
  public:
    [[nodiscard]] FractalPushConstants
    push_constants(const cubey::Camera2D& camera, std::uint32_t width, std::uint32_t height) const {
        const Camera2DView view =
            camera.view(static_cast<float>(width), static_cast<float>(height));
        FractalPushConstants constants;
        constants.center_x = view.center.x;
        constants.center_y = view.center.y;
        constants.scale = view.scale;
        constants.aspect = view.aspect;
        constants.max_iterations = max_iterations_;
        return constants;
    }

  private:
    std::int32_t max_iterations_ = 180;
};

} // namespace cubey::examples::fractal
