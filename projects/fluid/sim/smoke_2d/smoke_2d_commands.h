#pragma once

#include "smoke_2d_config.h"
#include "smoke_2d_gpu_resources.h"
#include "smoke_2d_injectors.h"

#include <cubey/engine/project_runtime.h>
#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace cubey::vulkan {
class GpuTimestampProfiler;
} // namespace cubey::vulkan

namespace cubey::projects::fluid::smoke_2d {

void record_smoke_compute(VkCommandBuffer command_buffer, Smoke2DGpuResources& resources,
                          const Smoke2DConfig& config, bool paused, bool& reset_requested,
                          const ProjectFrame& frame, std::span<const Smoke2DInjectorGpu> injectors,
                          bool include_render_visibility_barrier = true,
                          cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
                          std::uint32_t frame_slot_index = 0);

void record_fullscreen_draw(VkCommandBuffer command_buffer, const Smoke2DGpuResources& resources,
                            const Smoke2DConfig& config, Smoke2DDebugView debug_view,
                            cubey::render::ColorTargetView color_target,
                            cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
                            std::uint32_t frame_slot_index = 0);

[[nodiscard]] cubey::render::CompiledRenderGraph build_smoke_frame_graph(
    cubey::render::ColorTargetView color_target, Smoke2DGpuResources& resources,
    const Smoke2DConfig& config, Smoke2DDebugView debug_view, bool paused, bool& reset_requested,
    const ProjectFrame& frame, std::span<const Smoke2DInjectorGpu> injectors,
    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr, std::uint32_t frame_slot_index = 0);

} // namespace cubey::projects::fluid::smoke_2d
