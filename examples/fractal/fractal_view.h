#pragma once

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
    [[nodiscard]] FractalPushConstants push_constants(std::uint32_t width,
                                                      std::uint32_t height) const {
        FractalPushConstants constants;
        constants.center_x = center_x_;
        constants.center_y = center_y_;
        constants.scale = scale_;
        if (height != 0) {
            constants.aspect = static_cast<float>(width) / static_cast<float>(height);
        }
        constants.max_iterations = max_iterations_;
        return constants;
    }

    void set_view(float center_x, float center_y, float scale) {
        center_x_ = center_x;
        center_y_ = center_y;
        scale_ = scale;
    }

  private:
    float center_x_ = -0.5F;
    float center_y_ = 0.0F;
    float scale_ = 1.35F;
    std::int32_t max_iterations_ = 180;
};

} // namespace cubey::examples::fractal
