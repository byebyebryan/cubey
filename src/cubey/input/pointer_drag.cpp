#include <cubey/input/pointer_drag.h>

namespace cubey::input {

PointerDrag::PointerDrag(MouseButton button) : button_(button) {}

void PointerDrag::update(const InputFrame& input) {
    active_ = input.mouse_button_down(button_);
    has_cursor_ = active_ && input.has_cursor();
    cursor_ = has_cursor_ ? input.cursor() : CursorPosition{};
    delta_ = active_ ? input.mouse_button_delta(button_) : PointerDelta{};
    if (active_) {
        accumulated_delta_.x += delta_.x;
        accumulated_delta_.y += delta_.y;
    } else {
        accumulated_delta_ = {};
    }
}

PointerDelta PointerDrag::consume_accumulated_delta() {
    const PointerDelta consumed = accumulated_delta_;
    accumulated_delta_ = {};
    return consumed;
}

} // namespace cubey::input
