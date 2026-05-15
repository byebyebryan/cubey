#pragma once

#include <cubey/render/render_graph_compiled.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image.h>

#include <optional>
#include <vector>

namespace cubey::vulkan {
class Device;
} // namespace cubey::vulkan

namespace cubey::render {

class RenderGraphResourceSet {
  public:
    explicit RenderGraphResourceSet(const CompiledRenderGraph& graph);
    RenderGraphResourceSet(const cubey::vulkan::Device& device, const CompiledRenderGraph& graph);
    ~RenderGraphResourceSet() = default;

    RenderGraphResourceSet(const RenderGraphResourceSet&) = delete;
    RenderGraphResourceSet& operator=(const RenderGraphResourceSet&) = delete;
    RenderGraphResourceSet(RenderGraphResourceSet&& other) noexcept = default;
    RenderGraphResourceSet& operator=(RenderGraphResourceSet&& other) noexcept = default;

    void bind_texture(RenderGraphTextureHandle handle, RenderGraphResolvedTexture texture);
    void bind_buffer(RenderGraphBufferHandle handle, RenderGraphResolvedBuffer buffer);

    [[nodiscard]] std::optional<RenderGraphResolvedTexture>
    texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] std::optional<RenderGraphResolvedBuffer>
    buffer(RenderGraphBufferHandle handle) const;

  private:
    void allocate_transients(const cubey::vulkan::Device& device, const CompiledRenderGraph& graph);

    std::vector<std::optional<RenderGraphResolvedTexture>> textures_{};
    std::vector<std::optional<RenderGraphResolvedBuffer>> buffers_{};
    std::vector<cubey::vulkan::Image> transient_textures_{};
    std::vector<cubey::vulkan::Buffer> transient_buffers_{};
};

} // namespace cubey::render
