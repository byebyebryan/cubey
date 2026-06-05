#pragma once

#include <array>
#include <cstddef>

namespace cubey::input {

enum class Key {
    Unknown,
    A,
    D,
    Escape,
    R,
    S,
    Space,
    W,
};

enum class KeyAction {
    Unknown,
    Press,
    Release,
    Repeat,
};

struct KeyEvent {
    Key key = Key::Unknown;
    KeyAction action = KeyAction::Unknown;
    int native_key = 0;
    int native_scancode = 0;
    int native_mods = 0;
};

enum class MouseButton {
    Unknown,
    Left,
    Middle,
    Right,
};

enum class MouseButtonAction {
    Unknown,
    Press,
    Release,
};

struct CursorPosition {
    double x = 0.0;
    double y = 0.0;
};

struct PointerDelta {
    double x = 0.0;
    double y = 0.0;
};

struct MouseButtonEvent {
    MouseButton button = MouseButton::Unknown;
    MouseButtonAction action = MouseButtonAction::Unknown;
    CursorPosition cursor{};
    int native_button = 0;
    int native_mods = 0;
};

struct CursorPositionEvent {
    CursorPosition cursor{};
};

struct ScrollEvent {
    double x_offset = 0.0;
    double y_offset = 0.0;
    CursorPosition cursor{};
};

class InputFrame {
  public:
    [[nodiscard]] bool key_down(Key key) const;
    [[nodiscard]] bool key_pressed(Key key) const;
    [[nodiscard]] bool key_released(Key key) const;

    [[nodiscard]] bool mouse_button_down(MouseButton button) const;
    [[nodiscard]] bool mouse_button_pressed(MouseButton button) const;
    [[nodiscard]] bool mouse_button_released(MouseButton button) const;
    [[nodiscard]] PointerDelta mouse_button_delta(MouseButton button) const;

    [[nodiscard]] bool has_cursor() const {
        return has_cursor_;
    }
    [[nodiscard]] CursorPosition cursor() const {
        return cursor_;
    }
    [[nodiscard]] PointerDelta cursor_delta() const {
        return cursor_delta_;
    }
    [[nodiscard]] PointerDelta scroll_delta() const {
        return scroll_delta_;
    }

  private:
    friend class InputState;

    static constexpr std::size_t kKeyCount = 7;
    static constexpr std::size_t kMouseButtonCount = 3;

    std::array<bool, kKeyCount> keys_down_{};
    std::array<bool, kKeyCount> keys_pressed_{};
    std::array<bool, kKeyCount> keys_released_{};
    std::array<bool, kMouseButtonCount> mouse_buttons_down_{};
    std::array<bool, kMouseButtonCount> mouse_buttons_pressed_{};
    std::array<bool, kMouseButtonCount> mouse_buttons_released_{};
    std::array<PointerDelta, kMouseButtonCount> mouse_button_deltas_{};
    bool has_cursor_ = false;
    CursorPosition cursor_{};
    PointerDelta cursor_delta_{};
    PointerDelta scroll_delta_{};
};

class FilteredInputFrame {
  public:
    FilteredInputFrame(const InputFrame& input, bool mouse_enabled, bool keyboard_enabled)
        : input_(input), mouse_enabled_(mouse_enabled), keyboard_enabled_(keyboard_enabled) {}

    [[nodiscard]] bool mouse_enabled() const {
        return mouse_enabled_;
    }
    [[nodiscard]] bool keyboard_enabled() const {
        return keyboard_enabled_;
    }

    [[nodiscard]] bool key_down(Key key) const {
        return keyboard_enabled_ && input_.key_down(key);
    }
    [[nodiscard]] bool key_pressed(Key key) const {
        return keyboard_enabled_ && input_.key_pressed(key);
    }
    [[nodiscard]] bool key_released(Key key) const {
        return keyboard_enabled_ && input_.key_released(key);
    }

    [[nodiscard]] bool mouse_button_down(MouseButton button) const {
        return mouse_enabled_ && input_.mouse_button_down(button);
    }
    [[nodiscard]] bool mouse_button_pressed(MouseButton button) const {
        return mouse_enabled_ && input_.mouse_button_pressed(button);
    }
    [[nodiscard]] bool mouse_button_released(MouseButton button) const {
        return mouse_enabled_ && input_.mouse_button_released(button);
    }
    [[nodiscard]] PointerDelta mouse_button_delta(MouseButton button) const {
        return mouse_enabled_ ? input_.mouse_button_delta(button) : PointerDelta{};
    }

    [[nodiscard]] bool has_cursor() const {
        return mouse_enabled_ && input_.has_cursor();
    }
    [[nodiscard]] CursorPosition cursor() const {
        return mouse_enabled_ ? input_.cursor() : CursorPosition{};
    }
    [[nodiscard]] PointerDelta cursor_delta() const {
        return mouse_enabled_ ? input_.cursor_delta() : PointerDelta{};
    }
    [[nodiscard]] PointerDelta scroll_delta() const {
        return mouse_enabled_ ? input_.scroll_delta() : PointerDelta{};
    }

  private:
    const InputFrame& input_;
    bool mouse_enabled_ = true;
    bool keyboard_enabled_ = true;
};

class InputState {
  public:
    void begin_frame();
    void record_key(const KeyEvent& event);
    void record_mouse_button(const MouseButtonEvent& event);
    void record_cursor_position(const CursorPositionEvent& event);
    void record_scroll(const ScrollEvent& event);

    [[nodiscard]] const InputFrame& frame() const {
        return frame_;
    }

  private:
    InputFrame frame_;
};

} // namespace cubey::input
