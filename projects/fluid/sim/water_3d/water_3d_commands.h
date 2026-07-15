#pragma once

#include "water_3d_config.h"
#include "water_3d_gpu_resources.h"

#include <cubey/core/math.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/render_graph_frame.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey {
class CloudEnvironmentRuntime;
struct CloudEnvironmentRuntimeFrame;
} // namespace cubey

namespace cubey::projects::fluid::water_3d {

struct Water3DRenderCamera {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec3 position{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
    float fovy_radians = 1.0F;
};

enum class Water3DRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

void record_water_3d_compute(VkCommandBuffer command_buffer, Water3DGpuResources& resources,
                             const Water3DConfig& config, Water3DRuntimeState& runtime_state,
                             cubey::render::FrameSlot frame_slot, bool paused,
                             bool& reset_requested, const ProjectFrame& frame,
                             bool include_render_visibility_barrier = true,
                             cubey::vulkan::GpuTimestampProfiler* profiler = nullptr);

void record_water_3d_draw(VkCommandBuffer command_buffer, const Water3DGpuResources& resources,
                          const Water3DConfig& config, cubey::render::FrameSlot frame_slot,
                          const Water3DRuntimeState& runtime_state, Water3DRenderView render_view,
                          const Water3DRenderCamera& camera,
                          cubey::render::ColorTargetView color_target,
                          cubey::vulkan::GpuTimestampProfiler* profiler = nullptr);

void record_water_3d_surface_draw(
    VkCommandBuffer command_buffer, const cubey::vulkan::Device& device,
    cubey::render::RenderGraphFrameExecutor& graph_executor, Water3DGpuResources& resources,
    const Water3DConfig& config, cubey::render::FrameSlot frame_slot,
    const Water3DRuntimeState& runtime_state, Water3DRenderView render_view,
    const Water3DRenderCamera& camera, cubey::render::ColorTargetView color_target,
    Water3DRenderTargetMode target_mode, const Water3DEnvironmentTextureBindings& environment,
    bool moon_body_enabled, cubey::CloudEnvironmentRuntime* clouds = nullptr,
    const cubey::CloudEnvironmentRuntimeFrame* cloud_frame = nullptr,
    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr);

} // namespace cubey::projects::fluid::water_3d
