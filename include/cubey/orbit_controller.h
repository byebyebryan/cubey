#pragma once

#include <cubey/input.h>

namespace cubey {

class OrbitController {
  public:
    void set_auto_rotation_speed(float radians_per_second);

    void update(double delta_seconds);
    void reset();
    void toggle_pause();

    void begin_drag(double x, double y);
    void drag_to(double x, double y);
    void end_drag();
    void update_from_input(const cubey::input::InputFrame& input, double delta_seconds);

    float yaw() const {
        return yaw_;
    }
    float pitch() const {
        return pitch_;
    }
    bool paused() const {
        return paused_;
    }
    bool dragging() const {
        return dragging_;
    }

  private:
    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    float auto_rotation_speed_ = 0.0F;
    bool paused_ = false;
    bool dragging_ = false;
    double last_x_ = 0.0;
    double last_y_ = 0.0;
};

} // namespace cubey
