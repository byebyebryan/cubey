#include <cubey/orbit_controller.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_frame_clock_tracks_delta_elapsed_and_index();
void test_frame_stats_publish_window_title_metrics();

int main() {
    try {
        test_frame_clock_tracks_delta_elapsed_and_index();
        test_frame_stats_publish_window_title_metrics();

        cubey::OrbitController controller;

        require_close(controller.yaw(), 0.0F, "initial yaw should be zero");
        require_close(controller.pitch(), 0.0F, "initial pitch should be zero");
        require(!controller.paused(), "controller should start unpaused");

        controller.set_auto_rotation_speed(0.5F);
        controller.update(2.0);
        require_close(controller.yaw(), 1.0F, "auto rotation should advance yaw");

        controller.toggle_pause();
        controller.update(10.0);
        require_close(controller.yaw(), 1.0F, "paused auto rotation should not advance yaw");

        controller.begin_drag(10.0, 10.0);
        controller.drag_to(30.0, 5.0);
        controller.end_drag();
        require_close(controller.yaw(), 1.2F, "horizontal drag should adjust yaw");
        require_close(controller.pitch(), -0.05F, "vertical drag should adjust pitch");

        controller.reset();
        require_close(controller.yaw(), 0.0F, "reset should clear yaw");
        require_close(controller.pitch(), 0.0F, "reset should clear pitch");
        require(!controller.paused(), "reset should resume animation");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "cubey_core_tests: %s\n", error.what());
        return 1;
    }
}
