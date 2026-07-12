#include "terrain_surface_controller.h"

#include <algorithm>
#include <cmath>

namespace cubey::projects::terrain {
namespace {

constexpr float kLookRadiansPerPixel = 0.0045F;

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

void TerrainSurfaceController::set_home_speed_mps(float speed_mps) {
    if (!std::isfinite(speed_mps) || speed_mps <= 0.0F) {
        return;
    }
    home_speed_mps_ = speed_mps;
    speed_mps_ = speed_mps;
}

void TerrainSurfaceController::advance_forward(double delta_seconds) {
    const cubey::math::Vec2 forward{std::sin(yaw_radians_), -std::cos(yaw_radians_)};
    position_xz_ += forward * speed_mps_ * static_cast<float>(delta_seconds);
}

void TerrainSurfaceController::update(const cubey::input::FilteredInputFrame& input,
                                      double delta_seconds) {
    if (input.key_pressed(cubey::input::Key::R)) {
        reset();
    }
    if (input.mouse_enabled() && input.mouse_button_down(cubey::input::MouseButton::Left)) {
        const cubey::input::PointerDelta delta =
            input.mouse_button_delta(cubey::input::MouseButton::Left);
        yaw_radians_ -= static_cast<float>(delta.x) * kLookRadiansPerPixel;
        pitch_radians_ -= static_cast<float>(delta.y) * kLookRadiansPerPixel;
        pitch_radians_ = std::clamp(pitch_radians_, -1.20F, 0.65F);
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
