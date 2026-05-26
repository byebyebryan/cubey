#include <cubey/input/pan_zoom_2d_controller.h>

#include <cmath>

namespace cubey::input {

PanZoom2DController::PanZoom2DController(cubey::Camera2D camera, PanZoom2DConfig config)
    : camera_(camera), config_(config) {}

void PanZoom2DController::update_from_input(const InputFrame& input, float width, float height) {
    apply_input(input.mouse_button_down(config_.drag_button),
                input.mouse_button_delta(config_.drag_button), input.scroll_delta(),
                input.has_cursor(), input.cursor(), width, height);
}

void PanZoom2DController::update_from_input(const FilteredInputFrame& input, float width,
                                            float height) {
    apply_input(input.mouse_button_down(config_.drag_button),
                input.mouse_button_delta(config_.drag_button), input.scroll_delta(),
                input.has_cursor(), input.cursor(), width, height);
}

void PanZoom2DController::apply_input(bool drag_down, PointerDelta drag_delta, PointerDelta scroll,
                                      bool has_cursor, CursorPosition cursor, float width,
                                      float height) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    if (drag_down) {
        camera_.pan_by_screen_delta(
            {static_cast<float>(drag_delta.x), static_cast<float>(drag_delta.y)}, width, height);
    }

    if (scroll.y != 0.0 && has_cursor) {
        const float factor = std::pow(config_.zoom_base, static_cast<float>(scroll.y));
        camera_.zoom_at(factor, {static_cast<float>(cursor.x), static_cast<float>(cursor.y)}, width,
                        height);
    }
}

void PanZoom2DController::reset() {
    camera_.reset();
}

} // namespace cubey::input
