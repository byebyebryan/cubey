#include <cubey/render/frame_data.h>

#include <stdexcept>

namespace cubey::render {

FrameSlot single_frame_slot() {
    return {};
}

FrameSlot frame_slot_for_index(std::uint64_t frame_index, std::uint32_t slot_count) {
    if (slot_count == 0) {
        throw std::runtime_error("frame slot count must be positive");
    }

    return {
        .index = static_cast<std::uint32_t>(frame_index % slot_count),
        .count = slot_count,
    };
}

void validate_frame_slot(FrameSlot slot) {
    if (slot.count == 0) {
        throw std::runtime_error("frame slot count must be positive");
    }
    if (slot.index >= slot.count) {
        throw std::runtime_error("frame slot index must be less than frame slot count");
    }
}

} // namespace cubey::render
