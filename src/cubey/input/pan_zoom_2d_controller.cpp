#include <cubey/input/pan_zoom_2d_controller.h>

#include <cmath>

namespace cubey::input {

PanZoom2DController::PanZoom2DController(cubey::Camera2D camera, PanZoom2DConfig config)
    : camera_(camera), config_(config) {}

void PanZoom2DController::update_from_input(const InputFrame& input, float width, float height) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    if (input.mouse_button_down(config_.drag_button)) {
        const PointerDelta delta = input.mouse_button_delta(config_.drag_button);
        camera_.pan_by_screen_delta({static_cast<float>(delta.x), static_cast<float>(delta.y)},
                                    width, height);
    }

    const PointerDelta scroll = input.scroll_delta();
    if (scroll.y != 0.0 && input.has_cursor()) {
        const CursorPosition cursor = input.cursor();
        const float factor = std::pow(config_.zoom_base, static_cast<float>(scroll.y));
        camera_.zoom_at(factor, {static_cast<float>(cursor.x), static_cast<float>(cursor.y)}, width,
                        height);
    }
}

void PanZoom2DController::reset() {
    camera_.reset();
}

} // namespace cubey::input
