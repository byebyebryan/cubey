#pragma once

#include <cubey/input/input.h>

namespace cubey {

struct OrbitControllerConfig {
    float distance = 4.2F;
    float min_distance = 0.35F;
    float max_distance = 80.0F;
    float zoom_base = 0.86F;
};

class OrbitController {
  public:
    explicit OrbitController(OrbitControllerConfig config = {});

    void set_auto_rotation_speed(float radians_per_second);
    void set_distance_limits(float min_distance, float max_distance);
    void set_home_distance(float distance);
    void set_distance(float distance);

    void update(double delta_seconds);
    void reset();
    void toggle_pause();
    void zoom_by_scroll(double scroll_y);

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
    float distance() const {
        return distance_;
    }

  private:
    OrbitControllerConfig config_{};
    float distance_ = 4.2F;
    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    float auto_rotation_speed_ = 0.0F;
    bool paused_ = false;
    bool dragging_ = false;
    double last_x_ = 0.0;
    double last_y_ = 0.0;
};

} // namespace cubey
