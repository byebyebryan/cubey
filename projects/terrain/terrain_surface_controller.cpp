#include "terrain_surface_controller.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cubey::projects::terrain {
namespace {

constexpr float kLookRadiansPerPixel = 0.0045F;

[[nodiscard]] float normalize_angle(float angle_radians) {
    return std::remainder(angle_radians, 2.0F * std::numbers::pi_v<float>);
}

} // namespace

TerrainSurfaceController::TerrainSurfaceController(float home_speed_mps) {
    set_home_speed_mps(home_speed_mps);
}

void TerrainSurfaceController::reset() {
    position_xz_ = home_position_xz_;
    yaw_radians_ = home_yaw_radians_;
    pitch_radians_ = home_pitch_radians_;
    speed_mps_ = home_speed_mps_;
}

void TerrainSurfaceController::set_home_pose(cubey::math::Vec2 position_xz, float yaw_radians,
                                             float pitch_radians) {
    if (!std::isfinite(position_xz.x) || !std::isfinite(position_xz.y) ||
        !std::isfinite(yaw_radians) || !std::isfinite(pitch_radians)) {
        return;
    }
    home_position_xz_ = position_xz;
    home_yaw_radians_ = yaw_radians;
    home_pitch_radians_ = pitch_radians;
    reset();
}

void TerrainSurfaceController::set_home_constraints(float movement_radius_m,
                                                    float yaw_half_angle_radians) {
    if (!std::isfinite(movement_radius_m) || movement_radius_m < 0.0F ||
        !std::isfinite(yaw_half_angle_radians) || yaw_half_angle_radians < 0.0F) {
        return;
    }
    movement_radius_m_ = movement_radius_m;
    yaw_half_angle_radians_ = yaw_half_angle_radians;
    apply_home_constraints();
}

void TerrainSurfaceController::clear_home_constraints() {
    movement_radius_m_ = -1.0F;
    yaw_half_angle_radians_ = -1.0F;
}

void TerrainSurfaceController::set_home_speed_mps(float speed_mps) {
    if (!std::isfinite(speed_mps) || speed_mps <= 0.0F) {
        return;
    }
    home_speed_mps_ = speed_mps;
    speed_mps_ = speed_mps;
}

void TerrainSurfaceController::apply_look_delta(float yaw_delta_radians,
                                                float pitch_delta_radians) {
    if (!std::isfinite(yaw_delta_radians) || !std::isfinite(pitch_delta_radians)) {
        return;
    }
    yaw_radians_ += yaw_delta_radians;
    pitch_radians_ = std::clamp(pitch_radians_ + pitch_delta_radians, -1.20F, 0.65F);
    apply_home_constraints();
}

void TerrainSurfaceController::advance_forward(double delta_seconds) {
    const cubey::math::Vec2 forward{std::sin(yaw_radians_), -std::cos(yaw_radians_)};
    position_xz_ += forward * speed_mps_ * static_cast<float>(delta_seconds);
    apply_home_constraints();
}

void TerrainSurfaceController::update(const cubey::input::FilteredInputFrame& input,
                                      double delta_seconds) {
    if (input.key_pressed(cubey::input::Key::R)) {
        reset();
    }
    if (input.mouse_enabled() && input.mouse_button_down(cubey::input::MouseButton::Left)) {
        const cubey::input::PointerDelta delta =
            input.mouse_button_delta(cubey::input::MouseButton::Left);
        apply_look_delta(-static_cast<float>(delta.x) * kLookRadiansPerPixel,
                         -static_cast<float>(delta.y) * kLookRadiansPerPixel);
    }
    if (input.scroll_delta().y != 0.0) {
        speed_mps_ *= std::pow(1.18F, static_cast<float>(input.scroll_delta().y));
        speed_mps_ = std::clamp(speed_mps_, 2.0F, 1'200.0F);
    }

    cubey::math::Vec2 movement{0.0F, 0.0F};
    const cubey::math::Vec2 forward{std::sin(yaw_radians_), -std::cos(yaw_radians_)};
    const cubey::math::Vec2 right{std::cos(yaw_radians_), std::sin(yaw_radians_)};
    if (input.key_down(cubey::input::Key::W)) {
        movement += forward;
    }
    if (input.key_down(cubey::input::Key::S)) {
        movement -= forward;
    }
    if (input.key_down(cubey::input::Key::D)) {
        movement += right;
    }
    if (input.key_down(cubey::input::Key::A)) {
        movement -= right;
    }
    const float length = std::sqrt(movement.x * movement.x + movement.y * movement.y);
    if (length > 0.0F) {
        movement /= length;
        position_xz_ += movement * speed_mps_ * static_cast<float>(delta_seconds);
        apply_home_constraints();
    }
}

void TerrainSurfaceController::apply_home_constraints() {
    if (movement_radius_m_ >= 0.0F) {
        cubey::math::Vec2 offset = position_xz_ - home_position_xz_;
        const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y);
        if (length > movement_radius_m_ && length > 0.0F) {
            position_xz_ = home_position_xz_ + offset * (movement_radius_m_ / length);
        }
    }
    if (yaw_half_angle_radians_ >= 0.0F) {
        const float offset = normalize_angle(yaw_radians_ - home_yaw_radians_);
        yaw_radians_ = home_yaw_radians_ +
                       std::clamp(offset, -yaw_half_angle_radians_, yaw_half_angle_radians_);
    }
}

cubey::Transform3D TerrainSurfaceController::camera_transform(const TerrainSourceParameters& source,
                                                              float vertical_scale,
                                                              float clearance_m) const {
    const TerrainSample sample = sample_terrain(source, {.world_xz = position_xz_});
    return {
        .translation = {position_xz_.x, sample.height_m * vertical_scale + clearance_m,
                        position_xz_.y},
        .rotation = cubey::math::angle_axis_quat(yaw_radians_, {0.0F, 1.0F, 0.0F}) *
                    cubey::math::angle_axis_quat(pitch_radians_, {1.0F, 0.0F, 0.0F}),
    };
}

} // namespace cubey::projects::terrain
