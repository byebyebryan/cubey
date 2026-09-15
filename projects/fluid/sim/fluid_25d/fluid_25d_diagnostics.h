#pragma once

#include <cubey/core/profiling.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/frame_data.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

[[nodiscard]] std::uint64_t profile_frame_index(const ProjectFrame& frame);
[[nodiscard]] std::uint64_t collected_profile_frame_index(const ProjectFrame& frame,
                                                          cubey::render::FrameSlot frame_slot);

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings);

} // namespace cubey::projects::fluid::fluid_25d
