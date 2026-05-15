#pragma once

#include <cubey/render/render_graph_compiled.h>
#include <cubey/render/render_graph_resources.h>
#include <cubey/render/target.h>

namespace cubey::render {

[[nodiscard]] ColorTargetView resolved_color_target_view(const RenderGraphExecutionContext& context,
                                                         RenderGraphTextureHandle handle);
[[nodiscard]] RenderGraphSampledTextureView
resolved_sampled_texture_view(const RenderGraphExecutionContext& context,
                              RenderGraphTextureHandle handle);
[[nodiscard]] RenderGraphSampledTextureView
resolved_sampled_texture_view(const CompiledRenderGraph& graph,
                              const RenderGraphResourceSet& resources,
                              RenderGraphTextureHandle handle);

} // namespace cubey::render
