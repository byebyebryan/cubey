#include <cubey/pan_zoom_2d_controller.h>

#include <algorithm>
#include <cmath>

namespace cubey::input {

PanZoom2DController::PanZoom2DController(PanZoom2DConfig config) : config_(config) {
    reset();
}

void PanZoom2DController::update_from_input(const InputFrame& input, float width, float height) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    if (input.mouse_button_down(config_.drag_button)) {
        const PointerDelta delta = input.mouse_button_delta(config_.drag_button);
        pan_by_screen_delta(static_cast<float>(delta.x), static_cast<float>(delta.y), width,
                            height);
    }

    const PointerDelta scroll = input.scroll_delta();
    if (scroll.y != 0.0 && input.has_cursor()) {
        const CursorPosition cursor = input.cursor();
        const float factor = std::pow(config_.zoom_base, static_cast<float>(scroll.y));
        zoom_at(factor, static_cast<float>(cursor.x), static_cast<float>(cursor.y), width, height);
    }
}

void PanZoom2DController::pan_by_screen_delta(float delta_x, float delta_y, float width,
                                              float height) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }
    const float aspect = width / height;
    center_x_ -= (delta_x / width) * 2.0F * scale_ * aspect;
    center_y_ -= (delta_y / height) * 2.0F * scale_;
}

void PanZoom2DController::zoom_at(float factor, float cursor_x, float cursor_y, float width,
                                  float height) {
    if (factor <= 0.0F || width <= 0.0F || height <= 0.0F) {
        return;
    }

    const float aspect = width / height;
    const float screen_x = (cursor_x / width) * 2.0F - 1.0F;
    const float screen_y = 1.0F - (cursor_y / height) * 2.0F;
    const float before_x = center_x_ + screen_x * aspect * scale_;
    const float before_y = center_y_ + screen_y * scale_;

    scale_ = std::clamp(scale_ * factor, config_.min_scale, config_.max_scale);
    center_x_ = before_x - screen_x * aspect * scale_;
    center_y_ = before_y - screen_y * scale_;
}

void PanZoom2DController::reset() {
    center_x_ = config_.center_x;
    center_y_ = config_.center_y;
    scale_ = std::clamp(config_.scale, config_.min_scale, config_.max_scale);
}

} // namespace cubey::input
