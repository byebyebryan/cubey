#pragma once

#include <algorithm>
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

    void pan_by_screen_delta(float delta_x, float delta_y, float width, float height) {
        if (width <= 0.0F || height <= 0.0F) {
            return;
        }
        const float aspect = width / height;
        center_x_ -= (delta_x / width) * 2.0F * scale_ * aspect;
        center_y_ -= (delta_y / height) * 2.0F * scale_;
    }

    void zoom_at(float factor, float cursor_x, float cursor_y, float width, float height) {
        if (factor <= 0.0F || width <= 0.0F || height <= 0.0F) {
            return;
        }

        const float aspect = width / height;
        const float screen_x = (cursor_x / width) * 2.0F - 1.0F;
        const float screen_y = 1.0F - (cursor_y / height) * 2.0F;
        const float before_x = center_x_ + screen_x * aspect * scale_;
        const float before_y = center_y_ + screen_y * scale_;

        scale_ = std::clamp(scale_ * factor, kMinScale, kMaxScale);
        center_x_ = before_x - screen_x * aspect * scale_;
        center_y_ = before_y - screen_y * scale_;
    }

    void reset() {
        center_x_ = -0.5F;
        center_y_ = 0.0F;
        scale_ = 1.35F;
        max_iterations_ = 180;
    }

  private:
    static constexpr float kMinScale = 0.000001F;
    static constexpr float kMaxScale = 8.0F;

    float center_x_ = -0.5F;
    float center_y_ = 0.0F;
    float scale_ = 1.35F;
    std::int32_t max_iterations_ = 180;
};

} // namespace cubey::examples::fractal
