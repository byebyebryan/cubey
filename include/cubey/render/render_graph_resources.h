#pragma once

#include <cubey/render/render_graph_compiled.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image.h>

#include <optional>
#include <utility>
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

    [[nodiscard]] bool compatible(const CompiledRenderGraph& graph) const;
    void reset(const CompiledRenderGraph& graph);

    void bind_texture(RenderGraphTextureHandle handle, RenderGraphResolvedTexture texture);
    void bind_buffer(RenderGraphBufferHandle handle, RenderGraphResolvedBuffer buffer);

    [[nodiscard]] std::optional<RenderGraphResolvedTexture>
    texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] std::optional<RenderGraphResolvedBuffer>
    buffer(RenderGraphBufferHandle handle) const;

  private:
    struct TextureResourceKey {
        RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
        RenderGraphTextureDesc desc{};
        VkImageUsageFlags usage_flags = 0;
    };

    struct BufferResourceKey {
        RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
        RenderGraphBufferDesc desc{};
        VkBufferUsageFlags usage_flags = 0;
    };

    void capture_resource_keys(const CompiledRenderGraph& graph);
    void allocate_transients(const cubey::vulkan::Device& device, const CompiledRenderGraph& graph);
    void bind_transient_resources();

    std::vector<std::optional<RenderGraphResolvedTexture>> textures_{};
    std::vector<std::optional<RenderGraphResolvedBuffer>> buffers_{};
    std::vector<cubey::vulkan::Image> transient_textures_{};
    std::vector<cubey::vulkan::Buffer> transient_buffers_{};
    std::vector<std::pair<RenderGraphTextureHandle, RenderGraphResolvedTexture>>
        transient_texture_bindings_{};
    std::vector<std::pair<RenderGraphBufferHandle, RenderGraphResolvedBuffer>>
        transient_buffer_bindings_{};
    std::vector<TextureResourceKey> texture_keys_{};
    std::vector<BufferResourceKey> buffer_keys_{};
};

} // namespace cubey::render
