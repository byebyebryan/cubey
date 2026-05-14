#include <cubey/render/render_graph.h>

#include <stdexcept>

namespace cubey::render {
namespace {

[[nodiscard]] bool is_color_aspect(VkImageAspectFlags aspects) {
    return aspects == VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] bool is_depth_aspect(VkImageAspectFlags aspects) {
    return aspects == VK_IMAGE_ASPECT_DEPTH_BIT;
}

[[nodiscard]] VkImageLayout sampled_texture_layout(const RenderGraphTextureResource& resource) {
    if (is_color_aspect(resource.desc.aspects)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (is_depth_aspect(resource.desc.aspects)) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    throw std::runtime_error("render graph sampled texture requires color or depth aspect");
}

[[nodiscard]] RenderGraphSampledTextureView
sampled_texture_view(const RenderGraphTextureResource& resource,
                     RenderGraphResolvedTexture resolved) {
    if (resolved.image == VK_NULL_HANDLE || resolved.view == VK_NULL_HANDLE) {
        throw std::runtime_error("render graph sampled texture requires an allocated texture");
    }
    return {
        .image = resolved.image,
        .view = resolved.view,
        .layout = sampled_texture_layout(resource),
    };
}

[[nodiscard]] RenderGraphResolvedTexture resolved_texture(const CompiledRenderGraph& graph,
                                                          const RenderGraphResourceSet& resources,
                                                          RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = graph.texture(handle);
    const std::optional<RenderGraphResolvedTexture> resolved = resources.texture(handle);
    if (resolved.has_value()) {
        return resolved.value();
    }
    if (resource.lifetime == RenderGraphResourceLifetime::Imported) {
        return {
            .image = resource.imported_image,
            .view = resource.imported_view,
        };
    }
    throw std::runtime_error("render graph texture requires a resolved resource");
}

} // namespace

ColorTargetView resolved_color_target_view(const RenderGraphExecutionContext& context,
                                           RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = context.texture(handle);
    if (resource.desc.aspects != VK_IMAGE_ASPECT_COLOR_BIT) {
        throw std::runtime_error("render graph color target view requires a color texture");
    }
    const RenderGraphResolvedTexture resolved = context.resolved_texture(handle);
    return color_target_view(resource.desc.extent, resource.desc.format, resolved.image,
                             resolved.view);
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
    return sampled_texture_view(resource, resolved_texture(graph, resources, handle));
}

} // namespace cubey::render
