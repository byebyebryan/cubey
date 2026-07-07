#pragma once

#include <cubey/render/render_graph.h>

#include <optional>
#include <stdexcept>

namespace cubey::render::detail {

[[nodiscard]] inline bool is_render_graph_color_aspect(VkImageAspectFlags aspects) {
    return aspects == VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] inline bool is_render_graph_depth_aspect(VkImageAspectFlags aspects) {
    return aspects == VK_IMAGE_ASPECT_DEPTH_BIT;
}

[[nodiscard]] inline VkImageLayout
render_graph_sampled_texture_layout(const RenderGraphTextureResource& resource) {
    if (is_render_graph_color_aspect(resource.desc.aspects)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (is_render_graph_depth_aspect(resource.desc.aspects)) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    throw std::runtime_error("render graph sampled texture requires color or depth aspect");
}

[[nodiscard]] inline RenderGraphResolvedTexture
render_graph_resolved_texture(const CompiledRenderGraph& graph,
                              const RenderGraphResourceSet* resources,
                              RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = graph.texture(handle);
    if (resources != nullptr) {
        const std::optional<RenderGraphResolvedTexture> resolved = resources->texture(handle);
        if (resolved.has_value()) {
            return resolved.value();
        }
    }
    if (resource.lifetime == RenderGraphResourceLifetime::Imported) {
        return {
            .image = resource.imported_image,
            .view = resource.imported_view,
        };
    }
    throw std::runtime_error("render graph texture requires a resolved resource");
}

} // namespace cubey::render::detail
