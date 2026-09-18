#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_gpu_resources.h"

#include <cubey/core/math.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace cubey::projects::fluid::fluid_25d {

// The numerical terrain fields retain their metre-scale heights. The product
// view uses a restrained vertical presentation scale so the bounded River V0
// catchment remains readable from one deterministic orbit.
inline constexpr float kFluid25DCatchmentHeightScale = 0.08F;

struct Fluid25DRenderCamera {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec3 position{0.0F, 0.0F, 0.0F};
};

enum class Fluid25DRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

void record_fluid_25d_compute(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                              const Fluid25DConfig& config, bool paused, bool& reset_requested,
                              bool include_render_visibility_barrier = true,
                              cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
                              std::uint32_t frame_slot_index = 0U, float source_rate_scale = 1.0F);

void record_fluid_25d_compute_batch(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                                    const Fluid25DConfig& config,
                                    std::span<const float> source_rate_scales, bool paused,
                                    bool& reset_requested,
                                    bool include_render_visibility_barrier = true,
                                    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
                                    std::uint32_t frame_slot_index = 0U);

void record_fluid_25d_fullscreen_draw(VkCommandBuffer command_buffer,
                                      const Fluid25DGpuResources& resources,
                                      const Fluid25DConfig& config, Fluid25DDebugView debug_view,
                                      cubey::render::ColorTargetView color_target);

void record_fluid_25d_catchment_draw(VkCommandBuffer command_buffer,
                                     const Fluid25DGpuResources& resources,
                                     const Fluid25DConfig& config,
                                     Fluid25DCatchmentView catchment_view,
                                     const Fluid25DRenderCamera& camera,
                                     cubey::render::ColorTargetView color_target,
                                     cubey::render::DepthTargetView depth_target);

[[nodiscard]] cubey::render::CompiledRenderGraph build_fluid_25d_frame_graph(
    cubey::render::ColorTargetView color_target, Fluid25DGpuResources& resources,
    const Fluid25DConfig& config, Fluid25DPresentationView presentation_view,
    Fluid25DCatchmentView catchment_view, Fluid25DDebugView debug_view,
    const Fluid25DRenderCamera& camera,
    Fluid25DRenderTargetMode target_mode, bool include_simulation, bool paused,
    bool& reset_requested, bool& presentation_cue_reset_requested,
    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
    std::uint32_t frame_slot_index = 0U, std::span<const float> source_rate_scales = {});

} // namespace cubey::projects::fluid::fluid_25d
