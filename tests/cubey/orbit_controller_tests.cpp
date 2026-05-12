#include <cubey/input/orbit_controller.h>

#include <cubey/input/input.h>

#include <cmath>
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

void test_orbit_controller_tracks_rotation_drag_pause_and_reset() {
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
}

void test_orbit_controller_updates_from_input_snapshot() {
    cubey::OrbitController controller;
    cubey::input::InputState input;

    controller.set_auto_rotation_speed(0.5F);

    input.begin_frame();
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Press,
        .cursor = {.x = 10.0, .y = 10.0},
    });
    input.record_cursor_position({.cursor = {.x = 30.0, .y = 5.0}});
    controller.update_from_input(input.frame(), 2.0);

    require_close(controller.yaw(), 0.2F, "input drag should adjust yaw");
    require_close(controller.pitch(), -0.05F, "input drag should adjust pitch");
    require(controller.dragging(), "input button state should mark controller dragging");

    input.begin_frame();
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Release,
        .cursor = {.x = 30.0, .y = 5.0},
    });
    controller.update_from_input(input.frame(), 2.0);

    require_close(controller.yaw(), 1.2F, "auto rotation should resume after drag release");
    require(!controller.dragging(), "input release should clear dragging");
}
