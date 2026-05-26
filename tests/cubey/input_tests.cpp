#include <cubey/host/ui.h>
#include <cubey/input/input.h>
#include <cubey/input/pan_zoom_2d_controller.h>
#include <cubey/input/pointer_drag.h>
#include <cubey/scene/camera_2d.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(double actual, double expected, const char* message) {
    constexpr double kTolerance = 0.000001;
    if (actual < expected - kTolerance || actual > expected + kTolerance) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_input_state_tracks_key_and_button_edges() {
    cubey::input::InputState input;

    input.begin_frame();
    input.record_key({
        .key = cubey::input::Key::Space,
        .action = cubey::input::KeyAction::Press,
    });
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Press,
    });

    const cubey::input::InputFrame& pressed = input.frame();
    require(pressed.key_down(cubey::input::Key::Space), "pressed key should be down");
    require(pressed.key_pressed(cubey::input::Key::Space), "pressed key should expose edge");
    require(!pressed.key_released(cubey::input::Key::Space),
            "pressed key should not expose release edge");
    require(pressed.mouse_button_down(cubey::input::MouseButton::Left),
            "pressed mouse button should be down");
    require(pressed.mouse_button_pressed(cubey::input::MouseButton::Left),
            "pressed mouse button should expose edge");

    input.begin_frame();
    input.record_key({
        .key = cubey::input::Key::Space,
        .action = cubey::input::KeyAction::Repeat,
    });
    const cubey::input::InputFrame& held = input.frame();
    require(held.key_down(cubey::input::Key::Space), "held key should remain down");
    require(!held.key_pressed(cubey::input::Key::Space), "repeat should not create a pressed edge");
    require(held.mouse_button_down(cubey::input::MouseButton::Left),
            "held mouse button should remain down");
    require(!held.mouse_button_pressed(cubey::input::MouseButton::Left),
            "held mouse button should not create a pressed edge");

    input.begin_frame();
    input.record_key({
        .key = cubey::input::Key::Space,
        .action = cubey::input::KeyAction::Release,
    });
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Release,
    });

    const cubey::input::InputFrame& released = input.frame();
    require(!released.key_down(cubey::input::Key::Space), "released key should not be down");
    require(released.key_released(cubey::input::Key::Space),
            "released key should expose release edge");
    require(!released.mouse_button_down(cubey::input::MouseButton::Left),
            "released mouse button should not be down");
    require(released.mouse_button_released(cubey::input::MouseButton::Left),
            "released mouse button should expose release edge");
}

void test_input_state_accumulates_cursor_and_scroll_per_frame() {
    cubey::input::InputState input;

    input.begin_frame();
    input.record_cursor_position({.cursor = {.x = 10.0, .y = 20.0}});
    input.record_cursor_position({.cursor = {.x = 14.0, .y = 18.0}});
    input.record_scroll({.x_offset = 0.25, .y_offset = -1.5, .cursor = {.x = 14.0, .y = 18.0}});

    const cubey::input::InputFrame& frame = input.frame();
    require(frame.has_cursor(), "cursor should become available after cursor event");
    require_close(frame.cursor().x, 14.0, "cursor x should track latest position");
    require_close(frame.cursor().y, 18.0, "cursor y should track latest position");
    require_close(frame.cursor_delta().x, 4.0, "cursor delta x should accumulate");
    require_close(frame.cursor_delta().y, -2.0, "cursor delta y should accumulate");
    require_close(frame.scroll_delta().x, 0.25, "scroll delta x should accumulate");
    require_close(frame.scroll_delta().y, -1.5, "scroll delta y should accumulate");

    input.begin_frame();
    const cubey::input::InputFrame& next = input.frame();
    require(next.has_cursor(), "cursor availability should persist across frames");
    require_close(next.cursor().x, 14.0, "cursor x should persist across frames");
    require_close(next.cursor_delta().x, 0.0, "cursor delta should reset each frame");
    require_close(next.scroll_delta().y, 0.0, "scroll delta should reset each frame");
}

void test_input_state_ignores_unknown_inputs() {
    cubey::input::InputState input;

    input.begin_frame();
    input.record_key({
        .key = cubey::input::Key::Unknown,
        .action = cubey::input::KeyAction::Press,
    });
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Unknown,
        .action = cubey::input::MouseButtonAction::Press,
    });

    const cubey::input::InputFrame& frame = input.frame();
    require(!frame.key_down(cubey::input::Key::Unknown), "unknown key should not become down");
    require(!frame.key_pressed(cubey::input::Key::Unknown),
            "unknown key should not expose an edge");
    require(!frame.mouse_button_down(cubey::input::MouseButton::Unknown),
            "unknown mouse button should not become down");
    require(!frame.mouse_button_pressed(cubey::input::MouseButton::Unknown),
            "unknown mouse button should not expose an edge");
}

void test_filtered_input_frame_masks_captured_ui_channels() {
    cubey::input::InputState input;

    input.begin_frame();
    input.record_key({
        .key = cubey::input::Key::Space,
        .action = cubey::input::KeyAction::Press,
    });
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Press,
        .cursor = {.x = 12.0, .y = 18.0},
    });
    input.record_cursor_position({.cursor = {.x = 17.0, .y = 14.0}});
    input.record_scroll({.x_offset = 0.0, .y_offset = 2.0, .cursor = {.x = 17.0, .y = 14.0}});

    const cubey::input::FilteredInputFrame mouse_blocked(input.frame(), false, true);
    require(mouse_blocked.keyboard_enabled(), "keyboard should stay enabled");
    require(!mouse_blocked.mouse_enabled(), "mouse should be disabled");
    require(mouse_blocked.key_pressed(cubey::input::Key::Space),
            "keyboard events should pass when keyboard is enabled");
    require(!mouse_blocked.mouse_button_down(cubey::input::MouseButton::Left),
            "mouse down should be hidden when mouse is disabled");
    require(!mouse_blocked.has_cursor(), "cursor should be hidden when mouse is disabled");
    require_close(mouse_blocked.scroll_delta().y, 0.0, "scroll should be zeroed when disabled");

    const cubey::input::FilteredInputFrame keyboard_blocked(input.frame(), true, false);
    require(!keyboard_blocked.key_pressed(cubey::input::Key::Space),
            "keyboard events should be hidden when keyboard is disabled");
    require(keyboard_blocked.mouse_button_down(cubey::input::MouseButton::Left),
            "mouse state should pass when mouse is enabled");
    require(keyboard_blocked.has_cursor(), "cursor should pass when mouse is enabled");
    require_close(keyboard_blocked.cursor().x, 17.0, "cursor x should pass through");
    require_close(keyboard_blocked.scroll_delta().y, 2.0, "scroll should pass through");

    const auto ui_filtered =
        cubey::host::filtered_input(input.frame(), {.wants_mouse = true, .wants_keyboard = false});
    require(!ui_filtered.mouse_enabled(), "ui mouse capture should disable mouse reads");
    require(ui_filtered.keyboard_enabled(), "uncaptured keyboard should stay enabled");
}

void test_pointer_drag_tracks_active_cursor_and_accumulated_delta() {
    cubey::input::InputState input;
    cubey::input::PointerDrag drag;

    input.begin_frame();
    input.record_cursor_position({.cursor = {.x = 10.0, .y = 20.0}});
    drag.update(input.frame());
    require(!drag.active(), "drag should be inactive before the button is down");

    input.begin_frame();
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Press,
        .cursor = {.x = 10.0, .y = 20.0},
    });
    input.record_cursor_position({.cursor = {.x = 14.0, .y = 18.0}});
    drag.update(input.frame());
    require(drag.active(), "drag should become active while the button is down");
    require(drag.has_cursor(), "drag should expose the current cursor");
    require_close(drag.cursor().x, 14.0, "drag cursor should track latest cursor x");
    require_close(drag.delta().x, 4.0, "drag delta x should track button-held motion");
    require_close(drag.delta().y, -2.0, "drag delta y should track button-held motion");

    cubey::input::PointerDelta consumed = drag.consume_accumulated_delta();
    require_close(consumed.x, 4.0, "consume should return accumulated x");
    require_close(consumed.y, -2.0, "consume should return accumulated y");
    consumed = drag.consume_accumulated_delta();
    require_close(consumed.x, 0.0, "consume should clear accumulated x");
    require_close(consumed.y, 0.0, "consume should clear accumulated y");

    input.begin_frame();
    input.record_cursor_position({.cursor = {.x = 20.0, .y = 28.0}});
    drag.update(input.frame());
    consumed = drag.consume_accumulated_delta();
    require_close(consumed.x, 6.0, "drag should continue accumulating while held");
    require_close(consumed.y, 10.0, "drag should continue accumulating while held");

    input.begin_frame();
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Release,
        .cursor = {.x = 20.0, .y = 28.0},
    });
    drag.update(input.frame());
    require(!drag.active(), "drag should stop after release");
}

void test_pan_zoom_2d_controller_pans_and_zooms_from_input() {
    cubey::input::PanZoom2DController controller(cubey::Camera2D({
        .center = {-0.5F, 0.0F},
        .scale = 0.675F,
    }));
    cubey::input::InputState input;

    input.begin_frame();
    input.record_mouse_button({
        .button = cubey::input::MouseButton::Left,
        .action = cubey::input::MouseButtonAction::Press,
        .cursor = {.x = 320.0, .y = 180.0},
    });
    input.record_cursor_position({.cursor = {.x = 384.0, .y = 144.0}});
    controller.update_from_input(input.frame(), 640.0F, 360.0F);

    require_close(controller.camera().center().x, -0.74F,
                  "2D horizontal drag should pan in view units");
    require_close(controller.camera().center().y, 0.135F,
                  "2D vertical drag should follow screen motion");

    input.begin_frame();
    input.record_scroll({.x_offset = 0.0, .y_offset = 1.0, .cursor = {.x = 320.0, .y = 180.0}});
    controller.update_from_input(input.frame(), 640.0F, 360.0F);
    require_close(controller.camera().scale(), 0.5805F, "wheel up should zoom in at the cursor");

    controller.reset();
    require_close(controller.camera().center().x, -0.5F,
                  "reset should restore configured center x");
    require_close(controller.camera().center().y, 0.0F, "reset should restore configured center y");
    require_close(controller.camera().scale(), 0.675F, "reset should restore configured scale");
}
