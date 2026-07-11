#include "terrain_surface_controller.h"

#include <algorithm>
#include <cmath>

namespace cubey::projects::terrain {
namespace {

constexpr float kLookRadiansPerPixel = 0.0045F;

} // namespace

TerrainSurfaceController::TerrainSurfaceController() = default;

void TerrainSurfaceController::reset() {
    position_xz_ = {0.0F, 0.0F};
    yaw_radians_ = 0.62F;
    pitch_radians_ = -0.12F;
    speed_mps_ = 220.0F;
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
        speed_mps_ = std::clamp(speed_mps_, 20.0F, 1'200.0F);
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
