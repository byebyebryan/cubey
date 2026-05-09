#pragma once

#include <cubey/math.h>

namespace cubey {

struct Camera2DConfig {
    math::Vec2 center{0.0F, 0.0F};
    float scale = 1.0F;
    float min_scale = 0.000001F;
    float max_scale = 8.0F;
};

struct Camera2DView {
    math::Vec2 center{0.0F, 0.0F};
    float scale = 1.0F;
    float aspect = 1.0F;
};

class Camera2D {
  public:
    explicit Camera2D(Camera2DConfig config = {});

    void reset();
    void set_center(math::Vec2 center);
    void set_scale(float scale);
    void pan_by_screen_delta(math::Vec2 delta, float width, float height);
    void zoom_at(float factor, math::Vec2 cursor, float width, float height);

    [[nodiscard]] Camera2DView view(float width, float height) const;
    [[nodiscard]] math::Vec2 center() const {
        return center_;
    }
    [[nodiscard]] float scale() const {
        return scale_;
    }
    [[nodiscard]] float min_scale() const {
        return config_.min_scale;
    }
    [[nodiscard]] float max_scale() const {
        return config_.max_scale;
    }

  private:
    [[nodiscard]] float clamped_scale(float scale) const;

    Camera2DConfig config_;
    math::Vec2 center_{0.0F, 0.0F};
    float scale_ = 1.0F;
};

} // namespace cubey
