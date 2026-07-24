#pragma once

#include "pyro_3d_config.h"
#include "pyro_3d_gpu_resources.h"
#include "pyro_3d_sources.h"

#include <cubey/core/math.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/render_graph_frame.h>
#include <cubey/render/target.h>
#include <cubey/scene/camera_3d.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace cubey {
class TerrainBackdropRuntime;
}

namespace cubey::projects::fluid::pyro_3d {

struct Pyro3DFrameState {
    bool density_a_current = true;
    bool velocity_a_current = true;
    bool volumes_initialized = false;
    bool shadow_initialized = false;
    std::uint32_t frames_since_shadow_update = 0;
};

struct Pyro3DRenderCamera {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec3 position{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
    float fovy_radians = cubey::Camera3DConfig{}.fovy_radians;
    float near_plane_m = cubey::Camera3DConfig{}.near_z;
    float far_plane_m = cubey::Camera3DConfig{}.far_z;
};

enum class Pyro3DRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

void record_pyro_3d_compute(VkCommandBuffer command_buffer, Pyro3DGpuResources& resources,
                            const Pyro3DConfig& config, bool paused, bool& reset_requested,
                            const ProjectFrame& frame, std::span<const Pyro3DSourceGpu> sources,
                            Pyro3DFrameState& frame_state,
                            bool include_render_visibility_barrier = true,
                            cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
                            std::uint32_t frame_slot_index = 0);

void record_pyro_3d_draw(
    VkCommandBuffer command_buffer, const cubey::vulkan::Device& device,
    cubey::render::RenderGraphFrameExecutor& graph_executor, Pyro3DGpuResources& resources,
    const Pyro3DConfig& config, Pyro3DDebugView debug_view, const Pyro3DRenderCamera& camera,
    cubey::render::ColorTargetView color_target, Pyro3DRenderTargetMode target_mode,
    const Pyro3DFrameState& frame_state, cubey::TerrainBackdropRuntime* terrain = nullptr,
    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr, std::uint32_t frame_slot_index = 0,
    bool atmosphere_background_enabled = false, bool moon_body_enabled = false);

} // namespace cubey::projects::fluid::pyro_3d
