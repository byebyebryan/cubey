#include <cubey/input.h>

#include <algorithm>
#include <cstddef>
#include <optional>

namespace cubey::input {
namespace {

[[nodiscard]] std::optional<std::size_t> key_index(Key key) {
    switch (key) {
    case Key::D:
        return 0U;
    case Key::Escape:
        return 1U;
    case Key::R:
        return 2U;
    case Key::Space:
        return 3U;
    case Key::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> mouse_button_index(MouseButton button) {
    switch (button) {
    case MouseButton::Left:
        return 0U;
    case MouseButton::Middle:
        return 1U;
    case MouseButton::Right:
        return 2U;
    case MouseButton::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

bool InputFrame::key_down(Key key) const {
    const std::optional<std::size_t> index = key_index(key);
    return index.has_value() && keys_down_.at(index.value());
}

bool InputFrame::key_pressed(Key key) const {
    const std::optional<std::size_t> index = key_index(key);
    return index.has_value() && keys_pressed_.at(index.value());
}

bool InputFrame::key_released(Key key) const {
    const std::optional<std::size_t> index = key_index(key);
    return index.has_value() && keys_released_.at(index.value());
}

bool InputFrame::mouse_button_down(MouseButton button) const {
    const std::optional<std::size_t> index = mouse_button_index(button);
    return index.has_value() && mouse_buttons_down_.at(index.value());
}

bool InputFrame::mouse_button_pressed(MouseButton button) const {
    const std::optional<std::size_t> index = mouse_button_index(button);
    return index.has_value() && mouse_buttons_pressed_.at(index.value());
}

bool InputFrame::mouse_button_released(MouseButton button) const {
    const std::optional<std::size_t> index = mouse_button_index(button);
    return index.has_value() && mouse_buttons_released_.at(index.value());
}

PointerDelta InputFrame::mouse_button_delta(MouseButton button) const {
    const std::optional<std::size_t> index = mouse_button_index(button);
    if (!index.has_value()) {
        return {};
    }
    return mouse_button_deltas_.at(index.value());
}

void InputState::begin_frame() {
    std::ranges::fill(frame_.keys_pressed_, false);
    std::ranges::fill(frame_.keys_released_, false);
    std::ranges::fill(frame_.mouse_buttons_pressed_, false);
    std::ranges::fill(frame_.mouse_buttons_released_, false);
    std::ranges::fill(frame_.mouse_button_deltas_, PointerDelta{});
    frame_.cursor_delta_ = {};
    frame_.scroll_delta_ = {};
}

void InputState::record_key(const KeyEvent& event) {
    const std::optional<std::size_t> index = key_index(event.key);
    if (!index.has_value()) {
        return;
    }

    const std::size_t key = index.value();
    if (event.action == KeyAction::Press) {
        if (!frame_.keys_down_.at(key)) {
            frame_.keys_pressed_.at(key) = true;
        }
        frame_.keys_down_.at(key) = true;
    } else if (event.action == KeyAction::Release) {
        if (frame_.keys_down_.at(key)) {
            frame_.keys_released_.at(key) = true;
        }
        frame_.keys_down_.at(key) = false;
    } else if (event.action == KeyAction::Repeat) {
        frame_.keys_down_.at(key) = true;
    }
}

void InputState::record_mouse_button(const MouseButtonEvent& event) {
    const std::optional<std::size_t> index = mouse_button_index(event.button);
    if (!index.has_value()) {
        return;
    }

    const std::size_t button = index.value();
    if (event.action == MouseButtonAction::Press) {
        if (!frame_.mouse_buttons_down_.at(button)) {
            frame_.mouse_buttons_pressed_.at(button) = true;
        }
        frame_.mouse_buttons_down_.at(button) = true;
    } else if (event.action == MouseButtonAction::Release) {
        if (frame_.mouse_buttons_down_.at(button)) {
            frame_.mouse_buttons_released_.at(button) = true;
        }
        frame_.mouse_buttons_down_.at(button) = false;
    }
    frame_.has_cursor_ = true;
    frame_.cursor_ = event.cursor;
}

void InputState::record_cursor_position(const CursorPositionEvent& event) {
    if (frame_.has_cursor_) {
        const PointerDelta delta{
            .x = event.cursor.x - frame_.cursor_.x,
            .y = event.cursor.y - frame_.cursor_.y,
        };
        frame_.cursor_delta_.x += delta.x;
        frame_.cursor_delta_.y += delta.y;
        for (std::size_t i = 0; i < frame_.mouse_buttons_down_.size(); ++i) {
            if (frame_.mouse_buttons_down_.at(i)) {
                frame_.mouse_button_deltas_.at(i).x += delta.x;
                frame_.mouse_button_deltas_.at(i).y += delta.y;
            }
        }
    }
    frame_.has_cursor_ = true;
    frame_.cursor_ = event.cursor;
}

void InputState::record_scroll(const ScrollEvent& event) {
    frame_.scroll_delta_.x += event.x_offset;
    frame_.scroll_delta_.y += event.y_offset;
    frame_.has_cursor_ = true;
    frame_.cursor_ = event.cursor;
}

} // namespace cubey::input
