#include <cubey/render/frame_data.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(void (*fn)(), const char* message) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_frame_slot_defaults_to_single_frame_slot() {
    const cubey::render::FrameSlot slot = cubey::render::single_frame_slot();

    require(slot.index == 0, "single frame slot should use index zero");
    require(slot.count == 1, "single frame slot should expose one slot");
    cubey::render::validate_frame_slot(slot);
}

void test_frame_slot_wraps_frame_indices() {
    const cubey::render::FrameSlot first = cubey::render::frame_slot_for_index(0, 3);
    const cubey::render::FrameSlot second = cubey::render::frame_slot_for_index(1, 3);
    const cubey::render::FrameSlot third = cubey::render::frame_slot_for_index(2, 3);
    const cubey::render::FrameSlot wrapped = cubey::render::frame_slot_for_index(5, 3);

    require(first.index == 0 && first.count == 3, "frame slot should preserve slot count");
    require(second.index == 1, "frame slot should use frame index modulo slot count");
    require(third.index == 2, "frame slot should address the last slot before wrapping");
    require(wrapped.index == 2, "frame slot should wrap larger frame indices");
}

void test_frame_slot_rejects_invalid_slots() {
    require_throws([] { (void)cubey::render::frame_slot_for_index(0, 0); },
                   "frame slots should reject zero slot count");
    require_throws([] { cubey::render::validate_frame_slot({.index = 1, .count = 1}); },
                   "frame slots should reject index equal to count");
    require_throws([] { cubey::render::validate_frame_slot({.index = 0, .count = 0}); },
                   "frame slots should reject zero count");
}
