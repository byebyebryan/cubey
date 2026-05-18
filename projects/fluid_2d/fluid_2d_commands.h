#pragma once

#include "fluid_2d_config.h"
#include "fluid_2d_gpu_resources.h"
#include "fluid_2d_injectors.h"

#include <cubey/engine/project_runtime.h>
#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

#include <array>
#include <span>

namespace cubey::projects::fluid_2d {

struct FrameInjection {
    bool active = false;
    std::array<float, 2> xy{};
    std::array<float, 2> force{};
};

void record_fluid_compute(VkCommandBuffer command_buffer, Fluid2DGpuResources& resources,
                          const Fluid2DConfig& config, const FrameInjection& injection, bool paused,
                          bool& reset_requested, const ProjectFrame& frame,
                          std::span<const Fluid2DInjectorGpu> injectors,
                          bool include_render_visibility_barrier = true);

void record_fullscreen_draw(VkCommandBuffer command_buffer, const Fluid2DGpuResources& resources,
                            const Fluid2DConfig& config, FluidDebugView debug_view,
                            cubey::render::ColorTargetView color_target);

[[nodiscard]] cubey::render::CompiledRenderGraph
build_fluid_frame_graph(cubey::render::ColorTargetView color_target, Fluid2DGpuResources& resources,
                        const Fluid2DConfig& config, FluidDebugView debug_view,
                        const FrameInjection& injection, bool paused, bool& reset_requested,
                        const ProjectFrame& frame, std::span<const Fluid2DInjectorGpu> injectors);

} // namespace cubey::projects::fluid_2d
