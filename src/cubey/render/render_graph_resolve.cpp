#include <cubey/render/render_graph.h>

#include "render_graph_private.h"

#include <stdexcept>

namespace cubey::render {
namespace {

[[nodiscard]] RenderGraphSampledTextureView
sampled_texture_view(const RenderGraphTextureResource& resource,
                     RenderGraphResolvedTexture resolved) {
    if (resolved.image == VK_NULL_HANDLE || resolved.view == VK_NULL_HANDLE) {
        throw std::runtime_error("render graph sampled texture requires an allocated texture");
    }
    return {
        .image = resolved.image,
        .view = resolved.view,
        .layout = detail::render_graph_sampled_texture_layout(resource),
    };
}

} // namespace

ColorTargetView resolved_color_target_view(const RenderGraphExecutionContext& context,
                                           RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = context.texture(handle);
    if (resource.desc.aspects != VK_IMAGE_ASPECT_COLOR_BIT) {
        throw std::runtime_error("render graph color target view requires a color texture");
    }
    if (resource.desc.extent.depth != 1U) {
        throw std::runtime_error("render graph color target view requires a 2D texture");
    }
    const RenderGraphResolvedTexture resolved = context.resolved_texture(handle);
    return color_target_view({resource.desc.extent.width, resource.desc.extent.height},
                             resource.desc.format, resolved.image, resolved.view);
}

DepthTargetView resolved_depth_target_view(const RenderGraphExecutionContext& context,
                                           RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = context.texture(handle);
    if (resource.desc.aspects != VK_IMAGE_ASPECT_DEPTH_BIT) {
        throw std::runtime_error("render graph depth target view requires a depth texture");
    }
    if (resource.desc.extent.depth != 1U) {
        throw std::runtime_error("render graph depth target view requires a 2D texture");
    }
    const RenderGraphResolvedTexture resolved = context.resolved_texture(handle);
    return depth_target_view({resource.desc.extent.width, resource.desc.extent.height},
                             resource.desc.format, resolved.image, resolved.view);
}

RenderGraphSampledTextureView
resolved_sampled_texture_view(const RenderGraphExecutionContext& context,
                              RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = context.texture(handle);
    return sampled_texture_view(resource, context.resolved_texture(handle));
}

RenderGraphSampledTextureView resolved_sampled_texture_view(const CompiledRenderGraph& graph,
                                                            const RenderGraphResourceSet& resources,
                                                            RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = graph.texture(handle);
    return sampled_texture_view(resource,
                                detail::render_graph_resolved_texture(graph, &resources, handle));
}

} // namespace cubey::render
