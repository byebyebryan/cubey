#pragma once

#include "terrain_source.h"

#include <cubey/input/input.h>
#include <cubey/scene/transform_3d.h>

namespace cubey::projects::terrain {

class TerrainSurfaceController {
  public:
    explicit TerrainSurfaceController(float home_speed_mps = 220.0F);

    void reset();
    void set_home_pose(cubey::math::Vec2 position_xz, float yaw_radians, float pitch_radians);
    void set_home_constraints(float movement_radius_m, float yaw_half_angle_radians);
    void clear_home_constraints();
    void set_home_speed_mps(float speed_mps);
    void apply_look_delta(float yaw_delta_radians, float pitch_delta_radians);
    void advance_forward(double delta_seconds);
    void update(const cubey::input::FilteredInputFrame& input, double delta_seconds);
    [[nodiscard]] cubey::Transform3D camera_transform(const TerrainSourceParameters& source,
                                                      float vertical_scale,
                                                      float clearance_m) const;

  private:
    cubey::math::Vec2 home_position_xz_{0.0F, 0.0F};
    float home_yaw_radians_ = 0.62F;
    float home_pitch_radians_ = -0.12F;
    cubey::math::Vec2 position_xz_{home_position_xz_};
    float yaw_radians_ = home_yaw_radians_;
    float pitch_radians_ = home_pitch_radians_;
    float home_speed_mps_ = 220.0F;
    float speed_mps_ = 220.0F;
    float movement_radius_m_ = -1.0F;
    float yaw_half_angle_radians_ = -1.0F;

    void apply_home_constraints();
};

} // namespace cubey::projects::terrain
