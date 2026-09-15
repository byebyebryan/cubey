#include "fluid_25d_diagnostics.h"

namespace cubey::projects::fluid::fluid_25d {

std::uint64_t profile_frame_index(const ProjectFrame& frame) {
    return frame.frame_index == 0U ? 0U : frame.frame_index - 1U;
}

std::uint64_t collected_profile_frame_index(const ProjectFrame& frame,
                                            cubey::render::FrameSlot frame_slot) {
    if (frame.frame_index > frame_slot.count) {
        return frame.frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame);
}

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings) {
    if (recorder == nullptr) {
        return;
    }
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        recorder->record_gpu_span(frame_index, timing.label, timing.milliseconds);
    }
}

} // namespace cubey::projects::fluid::fluid_25d
