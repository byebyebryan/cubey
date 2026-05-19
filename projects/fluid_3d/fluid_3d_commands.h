#pragma once

#include "fluid_3d_config.h"
#include "fluid_3d_gpu_resources.h"
#include "fluid_3d_injectors.h"

#include <cubey/core/math.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/target.h>
#include <cubey/scene/camera_3d.h>

#include <vulkan/vulkan.h>

#include <span>

namespace cubey::projects::fluid_3d {

struct Fluid3DFrameState {
    bool density_a_current = true;
    bool velocity_a_current = true;
    bool volumes_initialized = false;
};

struct Fluid3DRenderCamera {
    cubey::math::Vec3 position{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
    float fovy_radians = cubey::Camera3DConfig{}.fovy_radians;
};

void record_fluid_3d_compute(VkCommandBuffer command_buffer, Fluid3DGpuResources& resources,
                             const Fluid3DConfig& config, bool paused, bool& reset_requested,
                             const ProjectFrame& frame,
                             std::span<const Fluid3DInjectorGpu> injectors,
                             Fluid3DFrameState& frame_state,
                             bool include_render_visibility_barrier = true);

void record_fluid_3d_draw(VkCommandBuffer command_buffer, const Fluid3DGpuResources& resources,
                          const Fluid3DConfig& config, Fluid3DDebugView debug_view,
                          const Fluid3DRenderCamera& camera,
                          cubey::render::ColorTargetView color_target,
                          const Fluid3DFrameState& frame_state);

} // namespace cubey::projects::fluid_3d
