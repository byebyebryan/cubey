#pragma once

#include <cubey/camera_2d.h>
#include <cubey/input.h>

namespace cubey::input {

struct PanZoom2DConfig {
    float zoom_base = 0.86F;
    MouseButton drag_button = MouseButton::Left;
};

class PanZoom2DController {
  public:
    explicit PanZoom2DController(cubey::Camera2D camera = cubey::Camera2D{},
                                 PanZoom2DConfig config = {});

    void update_from_input(const InputFrame& input, float width, float height);
    void reset();

    [[nodiscard]] Camera2D& camera() {
        return camera_;
    }
    [[nodiscard]] const Camera2D& camera() const {
        return camera_;
    }

  private:
    Camera2D camera_;
    PanZoom2DConfig config_;
};

} // namespace cubey::input
