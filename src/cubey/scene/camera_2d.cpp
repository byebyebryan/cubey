#include <cubey/scene/camera_2d.h>

#include <algorithm>

namespace cubey {

Camera2D::Camera2D(Camera2DConfig config) : config_(config) {
    reset();
}

void Camera2D::reset() {
    center_ = config_.center;
    scale_ = clamped_scale(config_.scale);
}

void Camera2D::set_center(math::Vec2 center) {
    center_ = center;
}

void Camera2D::set_scale(float scale) {
    scale_ = clamped_scale(scale);
}

void Camera2D::pan_by_screen_delta(math::Vec2 delta, float width, float height) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    const float aspect = width / height;
    center_.x -= (delta.x / width) * 2.0F * scale_ * aspect;
    center_.y -= (delta.y / height) * 2.0F * scale_;
}

void Camera2D::zoom_at(float factor, math::Vec2 cursor, float width, float height) {
    if (factor <= 0.0F || width <= 0.0F || height <= 0.0F) {
        return;
    }

    const float aspect = width / height;
    const float screen_x = (cursor.x / width) * 2.0F - 1.0F;
    const float screen_y = 1.0F - (cursor.y / height) * 2.0F;
    const float before_x = center_.x + screen_x * aspect * scale_;
    const float before_y = center_.y + screen_y * scale_;

    scale_ = clamped_scale(scale_ * factor);
    center_.x = before_x - screen_x * aspect * scale_;
    center_.y = before_y - screen_y * scale_;
}

Camera2DView Camera2D::view(float width, float height) const {
    const float aspect = (width > 0.0F && height > 0.0F) ? width / height : 1.0F;
    return {
        .center = center_,
        .scale = scale_,
        .aspect = aspect,
    };
}

float Camera2D::clamped_scale(float scale) const {
    return std::clamp(scale, config_.min_scale, config_.max_scale);
}

} // namespace cubey
