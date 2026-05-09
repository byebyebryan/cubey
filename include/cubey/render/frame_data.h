#pragma once

#include <cstdint>

namespace cubey::render {

inline constexpr std::uint32_t kSingleFrameSlotCount = 1;

struct FrameSlot {
    std::uint32_t index = 0;
    std::uint32_t count = kSingleFrameSlotCount;
};

[[nodiscard]] FrameSlot single_frame_slot();
[[nodiscard]] FrameSlot frame_slot_for_index(std::uint64_t frame_index,
                                             std::uint32_t slot_count);
void validate_frame_slot(FrameSlot slot);

} // namespace cubey::render
