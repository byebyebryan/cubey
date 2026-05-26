#pragma once

#include "water_2d_config.h"
#include "water_2d_gpu_resources.h"

#include <cubey/engine/project_runtime.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

namespace cubey::projects::fluid::water_2d {

enum class Water2DRenderTargetMode {
    Present,
    ColorAttachment,
};

struct Water2DFrameGraph {
    cubey::render::CompiledRenderGraph graph{};
    bool uses_surface_textures = false;
    cubey::render::RenderGraphTextureHandle raw_density{};
    cubey::render::RenderGraphTextureHandle surface_a{};
    cubey::render::RenderGraphTextureHandle surface_b{};
    cubey::render::RenderGraphTextureHandle final_surface{};
};

void record_water_2d_compute(VkCommandBuffer command_buffer, Water2DGpuResources& resources,
                             const Water2DConfig& config, Water2DRuntimeState& runtime_state,
                             cubey::render::FrameSlot frame_slot, bool paused,
                             bool& reset_requested, const ProjectFrame& frame,
                             bool include_render_visibility_barrier = true);

void record_water_2d_draw(VkCommandBuffer command_buffer, const Water2DGpuResources& resources,
                          const Water2DConfig& config, cubey::render::FrameSlot frame_slot,
                          const Water2DRuntimeState& runtime_state, Water2DDebugView debug_view,
                          cubey::render::ColorTargetView color_target);

[[nodiscard]] Water2DFrameGraph
build_water_2d_frame_graph(cubey::render::ColorTargetView color_target,
                           Water2DGpuResources& resources, const Water2DConfig& config,
                           Water2DRuntimeState& runtime_state, cubey::render::FrameSlot frame_slot,
                           Water2DDebugView debug_view, bool paused, bool& reset_requested,
                           const ProjectFrame& frame, Water2DRenderTargetMode target_mode,
                           bool include_simulation);

} // namespace cubey::projects::fluid::water_2d
