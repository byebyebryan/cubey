#pragma once

#include <cubey/input.h>

namespace cubey::input {

class PointerDrag {
  public:
    explicit PointerDrag(MouseButton button = MouseButton::Left);

    void update(const InputFrame& input);
    [[nodiscard]] PointerDelta consume_accumulated_delta();

    [[nodiscard]] bool active() const {
        return active_;
    }
    [[nodiscard]] bool has_cursor() const {
        return has_cursor_;
    }
    [[nodiscard]] CursorPosition cursor() const {
        return cursor_;
    }
    [[nodiscard]] PointerDelta delta() const {
        return delta_;
    }

  private:
    MouseButton button_ = MouseButton::Left;
    bool active_ = false;
    bool has_cursor_ = false;
    CursorPosition cursor_{};
    PointerDelta delta_{};
    PointerDelta accumulated_delta_{};
};

} // namespace cubey::input
