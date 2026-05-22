#pragma once

#include "water_3d_config.h"
#include "water_3d_gpu_resources.h"

#include <cubey/core/math.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/target.h>

#include <vulkan/vulkan.h>

namespace cubey::projects::fluid::water_3d {

struct Water3DRenderCamera {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
};

void record_water_3d_compute(VkCommandBuffer command_buffer, Water3DGpuResources& resources,
                             const Water3DConfig& config, Water3DRuntimeState& runtime_state,
                             cubey::render::FrameSlot frame_slot, bool paused,
                             bool& reset_requested, const ProjectFrame& frame,
                             bool include_render_visibility_barrier = true);

void record_water_3d_draw(VkCommandBuffer command_buffer, const Water3DGpuResources& resources,
                          const Water3DConfig& config, cubey::render::FrameSlot frame_slot,
                          const Water3DRuntimeState& runtime_state, Water3DDebugView debug_view,
                          const Water3DRenderCamera& camera,
                          cubey::render::ColorTargetView color_target);

} // namespace cubey::projects::fluid::water_3d
