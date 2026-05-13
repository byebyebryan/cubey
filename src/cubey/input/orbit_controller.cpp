#include <cubey/input/orbit_controller.h>

#include <algorithm>

namespace cubey {
namespace {

constexpr float kDragRadiansPerPixel = 0.01F;
constexpr float kMaxPitchRadians = 1.45F;

} // namespace

void OrbitController::set_auto_rotation_speed(float radians_per_second) {
    auto_rotation_speed_ = radians_per_second;
}

void OrbitController::update(double delta_seconds) {
    if (!paused_ && !dragging_) {
        yaw_ += auto_rotation_speed_ * static_cast<float>(delta_seconds);
    }
}

void OrbitController::reset() {
    yaw_ = 0.0F;
    pitch_ = 0.0F;
    paused_ = false;
    dragging_ = false;
    last_x_ = 0.0;
    last_y_ = 0.0;
}

void OrbitController::toggle_pause() {
    paused_ = !paused_;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void OrbitController::begin_drag(double x, double y) {
    dragging_ = true;
    last_x_ = x;
    last_y_ = y;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void OrbitController::drag_to(double x, double y) {
    if (!dragging_) {
        begin_drag(x, y);
        return;
    }

    yaw_ -= static_cast<float>(x - last_x_) * kDragRadiansPerPixel;
    pitch_ -= static_cast<float>(y - last_y_) * kDragRadiansPerPixel;
    pitch_ = std::clamp(pitch_, -kMaxPitchRadians, kMaxPitchRadians);
    last_x_ = x;
    last_y_ = y;
}

void OrbitController::end_drag() {
    dragging_ = false;
}

void OrbitController::update_from_input(const cubey::input::InputFrame& input,
                                        double delta_seconds) {
    if (input.key_pressed(cubey::input::Key::R)) {
        reset();
    }
    if (input.key_pressed(cubey::input::Key::Space)) {
        toggle_pause();
    }

    dragging_ = input.mouse_button_down(cubey::input::MouseButton::Left);
    if (dragging_) {
        const cubey::input::PointerDelta delta =
            input.mouse_button_delta(cubey::input::MouseButton::Left);
        yaw_ -= static_cast<float>(delta.x) * kDragRadiansPerPixel;
        pitch_ -= static_cast<float>(delta.y) * kDragRadiansPerPixel;
        pitch_ = std::clamp(pitch_, -kMaxPitchRadians, kMaxPitchRadians);
    }

    update(delta_seconds);
}

} // namespace cubey
