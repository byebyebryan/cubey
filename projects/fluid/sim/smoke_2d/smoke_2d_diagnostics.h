#pragma once

#include "smoke_2d_config.h"

#include <cubey/core/profiling.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/frame_data.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::fluid::smoke_2d {

[[nodiscard]] std::uint64_t profile_frame_index(const ProjectFrame& frame);
[[nodiscard]] std::uint64_t collected_profile_frame_index(const ProjectFrame& frame,
                                                          cubey::render::FrameSlot frame_slot);

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings);

[[nodiscard]] bool should_record_smoke_2d_diagnostics(cubey::profiling::ProfileRecorder* recorder,
                                                      const Smoke2DConfig& config,
                                                      std::uint64_t frame_index);

void record_smoke_2d_diagnostics(cubey::profiling::ProfileRecorder& recorder,
                                 std::uint64_t frame_index, const Smoke2DConfig& config,
                                 const std::vector<std::uint8_t>& field_bytes,
                                 const std::vector<std::uint8_t>& divergence_bytes,
                                 const std::vector<std::uint8_t>& curl_bytes);

} // namespace cubey::projects::fluid::smoke_2d
