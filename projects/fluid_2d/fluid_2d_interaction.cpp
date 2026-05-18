#include "fluid_2d_interaction.h"

#include <algorithm>

namespace cubey::projects::fluid_2d {

FrameInjection frame_injection_from_pointer(const cubey::input::CursorPosition& cursor,
                                            const cubey::input::PointerDelta& delta,
                                            VkExtent2D window_extent) {
    if (window_extent.width == 0 || window_extent.height == 0) {
        return {};
    }

    const float width = static_cast<float>(window_extent.width);
    const float height = static_cast<float>(window_extent.height);
    const float cursor_x = static_cast<float>(cursor.x);
    const float cursor_y = static_cast<float>(cursor.y);
    const float delta_x = static_cast<float>(delta.x);
    const float delta_y = static_cast<float>(delta.y);

    return {
        .active = true,
        .xy =
            {
                std::clamp(cursor_x / width, 0.0F, 1.0F),
                std::clamp(cursor_y / height, 0.0F, 1.0F),
            },
        .force =
            {
                std::clamp((delta_x / width) * 90.0F, -8.0F, 8.0F),
                std::clamp((delta_y / height) * 90.0F, -8.0F, 8.0F),
            },
    };
}

} // namespace cubey::projects::fluid_2d
