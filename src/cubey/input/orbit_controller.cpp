#include <cubey/input/orbit_controller.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey {
namespace {

constexpr float kDragRadiansPerPixel = 0.01F;

[[nodiscard]] OrbitControllerConfig validated_config(OrbitControllerConfig config) {
    if (!std::isfinite(config.distance) || !std::isfinite(config.min_distance) ||
        !std::isfinite(config.max_distance) || !std::isfinite(config.zoom_base) ||
        !std::isfinite(config.min_pitch) || !std::isfinite(config.max_pitch)) {
        throw std::invalid_argument("orbit controller config values must be finite");
    }
    if (config.min_distance <= 0.0F) {
        throw std::invalid_argument("orbit controller min distance must be positive");
    }
    if (config.max_distance < config.min_distance) {
        throw std::invalid_argument("orbit controller max distance must be >= min distance");
    }
    if (config.zoom_base <= 0.0F || config.zoom_base >= 1.0F) {
        throw std::invalid_argument("orbit controller zoom base must be between 0 and 1");
    }
    if (config.max_pitch < config.min_pitch) {
        throw std::invalid_argument("orbit controller max pitch must be >= min pitch");
    }
    config.distance = std::clamp(config.distance, config.min_distance, config.max_distance);
    return config;
}

} // namespace

OrbitController::OrbitController(OrbitControllerConfig config)
    : config_(validated_config(config)), distance_(config_.distance) {}

void OrbitController::set_auto_rotation_speed(float radians_per_second) {
    auto_rotation_speed_ = radians_per_second;
}

void OrbitController::set_distance_limits(float min_distance, float max_distance) {
    config_ = validated_config({
        .distance = config_.distance,
        .min_distance = min_distance,
        .max_distance = max_distance,
        .zoom_base = config_.zoom_base,
        .min_pitch = config_.min_pitch,
        .max_pitch = config_.max_pitch,
    });
    distance_ = std::clamp(distance_, config_.min_distance, config_.max_distance);
}

void OrbitController::set_pitch_limits(float min_pitch, float max_pitch) {
    config_ = validated_config({
        .distance = config_.distance,
        .min_distance = config_.min_distance,
        .max_distance = config_.max_distance,
        .zoom_base = config_.zoom_base,
        .min_pitch = min_pitch,
        .max_pitch = max_pitch,
    });
    pitch_ = std::clamp(pitch_, config_.min_pitch, config_.max_pitch);
}

void OrbitController::set_home_distance(float distance) {
    config_ = validated_config({
        .distance = distance,
        .min_distance = config_.min_distance,
        .max_distance = config_.max_distance,
        .zoom_base = config_.zoom_base,
        .min_pitch = config_.min_pitch,
        .max_pitch = config_.max_pitch,
    });
    distance_ = config_.distance;
}

void OrbitController::set_distance(float distance) {
    distance_ = std::clamp(distance, config_.min_distance, config_.max_distance);
}

void OrbitController::set_pitch(float pitch) {
    pitch_ = std::clamp(pitch, config_.min_pitch, config_.max_pitch);
}

void OrbitController::update(double delta_seconds) {
    if (!paused_ && !dragging_) {
        yaw_ += auto_rotation_speed_ * static_cast<float>(delta_seconds);
    }
}

void OrbitController::reset() {
    yaw_ = 0.0F;
    pitch_ = std::clamp(0.0F, config_.min_pitch, config_.max_pitch);
    paused_ = false;
    dragging_ = false;
    last_x_ = 0.0;
    last_y_ = 0.0;
    distance_ = config_.distance;
}

void OrbitController::toggle_pause() {
    paused_ = !paused_;
}

void OrbitController::zoom_by_scroll(double scroll_y) {
    if (scroll_y == 0.0) {
        return;
    }
    const float factor = std::pow(config_.zoom_base, static_cast<float>(scroll_y));
    set_distance(distance_ * factor);
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
    pitch_ = std::clamp(pitch_, config_.min_pitch, config_.max_pitch);
    last_x_ = x;
    last_y_ = y;
}

void OrbitController::end_drag() {
    dragging_ = false;
}

void OrbitController::update_pointer_input(const cubey::input::InputFrame& input,
                                           double delta_seconds) {
    apply_input(false, false, input.scroll_delta().y, true,
                input.mouse_button_down(cubey::input::MouseButton::Left),
                input.mouse_button_delta(cubey::input::MouseButton::Left), delta_seconds);
}

void OrbitController::update_pointer_input(const cubey::input::FilteredInputFrame& input,
                                           double delta_seconds) {
    apply_input(false, false, input.scroll_delta().y, input.mouse_enabled(),
                input.mouse_button_down(cubey::input::MouseButton::Left),
                input.mouse_button_delta(cubey::input::MouseButton::Left), delta_seconds);
}

void OrbitController::update_from_input(const cubey::input::InputFrame& input,
                                        double delta_seconds) {
    apply_input(input.key_pressed(cubey::input::Key::R),
                input.key_pressed(cubey::input::Key::Space), input.scroll_delta().y, true,
                input.mouse_button_down(cubey::input::MouseButton::Left),
                input.mouse_button_delta(cubey::input::MouseButton::Left), delta_seconds);
}

void OrbitController::update_from_input(const cubey::input::FilteredInputFrame& input,
                                        double delta_seconds) {
    apply_input(input.key_pressed(cubey::input::Key::R),
                input.key_pressed(cubey::input::Key::Space), input.scroll_delta().y,
                input.mouse_enabled(), input.mouse_button_down(cubey::input::MouseButton::Left),
                input.mouse_button_delta(cubey::input::MouseButton::Left), delta_seconds);
}

void OrbitController::apply_input(bool reset_pressed, bool pause_pressed, double scroll_y,
                                  bool mouse_enabled, bool mouse_down,
                                  cubey::input::PointerDelta mouse_delta, double delta_seconds) {
    if (reset_pressed) {
        reset();
    }
    if (pause_pressed) {
        toggle_pause();
    }

    zoom_by_scroll(scroll_y);

    if (!mouse_enabled) {
        dragging_ = false;
        update(delta_seconds);
        return;
    }

    dragging_ = mouse_down;
    if (dragging_) {
        yaw_ -= static_cast<float>(mouse_delta.x) * kDragRadiansPerPixel;
        pitch_ -= static_cast<float>(mouse_delta.y) * kDragRadiansPerPixel;
        pitch_ = std::clamp(pitch_, config_.min_pitch, config_.max_pitch);
    }

    update(delta_seconds);
}

} // namespace cubey
