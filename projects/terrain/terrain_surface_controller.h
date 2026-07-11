#pragma once

#include "terrain_source.h"

#include <cubey/input/input.h>
#include <cubey/scene/transform_3d.h>

namespace cubey::projects::terrain {

class TerrainSurfaceController {
  public:
    TerrainSurfaceController();

    void reset();
    void advance_forward(double delta_seconds);
    void update(const cubey::input::FilteredInputFrame& input, double delta_seconds);
    [[nodiscard]] cubey::Transform3D camera_transform(const TerrainSourceParameters& source,
                                                      float vertical_scale,
                                                      float clearance_m) const;

  private:
    cubey::math::Vec2 position_xz_{0.0F, 0.0F};
    float yaw_radians_ = 0.62F;
    float pitch_radians_ = -0.12F;
    float speed_mps_ = 220.0F;
};

} // namespace cubey::projects::terrain
