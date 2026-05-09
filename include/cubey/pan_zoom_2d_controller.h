#pragma once

#include <cubey/input.h>

namespace cubey::input {

struct PanZoom2DConfig {
    float center_x = 0.0F;
    float center_y = 0.0F;
    float scale = 1.0F;
    float min_scale = 0.000001F;
    float max_scale = 8.0F;
    float zoom_base = 0.86F;
    MouseButton drag_button = MouseButton::Left;
};

class PanZoom2DController {
  public:
    explicit PanZoom2DController(PanZoom2DConfig config = {});

    void update_from_input(const InputFrame& input, float width, float height);
    void pan_by_screen_delta(float delta_x, float delta_y, float width, float height);
    void zoom_at(float factor, float cursor_x, float cursor_y, float width, float height);
    void reset();

    [[nodiscard]] float center_x() const {
        return center_x_;
    }
    [[nodiscard]] float center_y() const {
        return center_y_;
    }
    [[nodiscard]] float scale() const {
        return scale_;
    }

  private:
    PanZoom2DConfig config_;
    float center_x_ = 0.0F;
    float center_y_ = 0.0F;
    float scale_ = 1.0F;
};

} // namespace cubey::input
