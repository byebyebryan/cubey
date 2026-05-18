#pragma once

#include "fluid_2d_commands.h"

#include <cubey/input/input.h>

#include <vulkan/vulkan.h>

namespace cubey::projects::fluid_2d {

[[nodiscard]] FrameInjection
frame_injection_from_pointer(const cubey::input::CursorPosition& cursor,
                             const cubey::input::PointerDelta& delta, VkExtent2D window_extent);

} // namespace cubey::projects::fluid_2d
