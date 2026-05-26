#pragma once

#include <cubey/input/input.h>
#include <cubey/scene/camera_2d.h>

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
    void update_from_input(const FilteredInputFrame& input, float width, float height);
    void reset();

    [[nodiscard]] Camera2D& camera() {
        return camera_;
    }
    [[nodiscard]] const Camera2D& camera() const {
        return camera_;
    }

  private:
    void apply_input(bool drag_down, PointerDelta drag_delta, PointerDelta scroll, bool has_cursor,
                     CursorPosition cursor, float width, float height);

    Camera2D camera_;
    PanZoom2DConfig config_;
};

} // namespace cubey::input
