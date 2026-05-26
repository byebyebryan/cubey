#pragma once

#include <cubey/input/input.h>

namespace cubey::host {

struct UiCaptureState {
    bool wants_mouse = false;
    bool wants_keyboard = false;
};

[[nodiscard]] inline cubey::input::FilteredInputFrame
filtered_input(const cubey::input::InputFrame& input, UiCaptureState ui_capture) {
    return cubey::input::FilteredInputFrame(input, !ui_capture.wants_mouse,
                                            !ui_capture.wants_keyboard);
}

} // namespace cubey::host
