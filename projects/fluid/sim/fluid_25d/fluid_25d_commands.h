#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_gpu_resources.h"

#include <cubey/engine/project_runtime.h>
#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

namespace cubey::projects::fluid::fluid_25d {

void record_fluid_25d_compute(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                              const Fluid25DConfig& config, bool paused, bool& reset_requested,
                              bool include_render_visibility_barrier = true);

void record_fluid_25d_fullscreen_draw(VkCommandBuffer command_buffer,
                                      const Fluid25DGpuResources& resources,
                                      const Fluid25DConfig& config, Fluid25DDebugView debug_view,
                                      cubey::render::ColorTargetView color_target);

[[nodiscard]] cubey::render::CompiledRenderGraph
build_fluid_25d_frame_graph(cubey::render::ColorTargetView color_target,
                            Fluid25DGpuResources& resources, const Fluid25DConfig& config,
                            Fluid25DDebugView debug_view, bool paused, bool& reset_requested);

} // namespace cubey::projects::fluid::fluid_25d
