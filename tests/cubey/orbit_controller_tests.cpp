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
    cubey::OrbitController controller(cubey::OrbitControllerConfig{.distance = 5.0F});

    require_close(controller.yaw(), 0.0F, "initial yaw should be zero");
    require_close(controller.pitch(), 0.0F, "initial pitch should be zero");
    require_close(controller.distance(), 5.0F, "initial distance should use configured home");
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
    require_close(controller.yaw(), 0.8F, "right drag should reduce orbit yaw");
    require_close(controller.pitch(), 0.05F, "up drag should raise orbit pitch");

    controller.reset();
    require_close(controller.yaw(), 0.0F, "reset should clear yaw");
    require_close(controller.pitch(), 0.0F, "reset should clear pitch");
    require_close(controller.distance(), 5.0F, "reset should restore home distance");
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
    input.record_scroll({.y_offset = 1.0, .cursor = {.x = 30.0, .y = 5.0}});
    controller.update_from_input(input.frame(), 2.0);

    require_close(controller.yaw(), -0.2F, "input right drag should reduce orbit yaw");
    require_close(controller.pitch(), 0.05F, "input up drag should raise orbit pitch");
    require_close(controller.distance(), 3.612F, "positive wheel scroll should zoom orbit in");
    require(controller.dragging(), "input button state should mark controller dragging");

    input.begin_frame();
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Release,
        .cursor = {.x = 30.0, .y = 5.0},
    });
    controller.update_from_input(input.frame(), 2.0);

    require_close(controller.yaw(), 0.8F, "auto rotation should resume after drag release");
    require(!controller.dragging(), "input release should clear dragging");
}

void test_orbit_controller_scroll_zoom_clamps_distance() {
    cubey::OrbitController controller(cubey::OrbitControllerConfig{
        .distance = 5.0F,
        .min_distance = 2.0F,
        .max_distance = 8.0F,
        .zoom_base = 0.5F,
    });

    controller.zoom_by_scroll(1.0);
    require_close(controller.distance(), 2.5F, "positive wheel scroll should reduce distance");

    controller.zoom_by_scroll(10.0);
    require_close(controller.distance(), 2.0F, "orbit zoom in should clamp to minimum distance");

    controller.zoom_by_scroll(-10.0);
    require_close(controller.distance(), 8.0F, "orbit zoom out should clamp to maximum distance");

    controller.set_distance_limits(1.0F, 4.0F);
    require_close(controller.distance(), 4.0F, "distance should reclamp when limits shrink");

    controller.set_home_distance(3.0F);
    controller.set_distance(2.0F);
    controller.reset();
    require_close(controller.distance(), 3.0F, "reset should restore updated home distance");
}

void test_orbit_controller_supports_configurable_pitch_limits() {
    cubey::OrbitController controller(cubey::OrbitControllerConfig{
        .min_pitch = -0.2F,
        .max_pitch = 0.1F,
    });

    controller.begin_drag(0.0, 0.0);
    controller.drag_to(0.0, -100.0);
    controller.end_drag();
    require_close(controller.pitch(), 0.1F, "orbit pitch should clamp to configured maximum");

    controller.begin_drag(0.0, 0.0);
    controller.drag_to(0.0, 100.0);
    controller.end_drag();
    require_close(controller.pitch(), -0.2F, "orbit pitch should clamp to configured minimum");

    controller.set_pitch_limits(-0.05F, 0.05F);
    require_close(controller.pitch(), -0.05F, "pitch should reclamp when limits shrink");
    controller.reset();
    require_close(controller.pitch(), 0.0F, "reset should return to zero inside pitch limits");
}
