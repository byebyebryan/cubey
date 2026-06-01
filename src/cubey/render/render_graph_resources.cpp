#include <cubey/render/render_graph.h>

#include <stdexcept>

namespace cubey::render {
namespace {

[[nodiscard]] VkImageUsageFlags image_usage_flags(RenderGraphTextureUsage usage) {
    switch (usage) {
    case RenderGraphTextureUsage::SampledRead:
        return VK_IMAGE_USAGE_SAMPLED_BIT;
    case RenderGraphTextureUsage::StorageRead:
    case RenderGraphTextureUsage::StorageWrite:
    case RenderGraphTextureUsage::StorageReadWrite:
        return VK_IMAGE_USAGE_STORAGE_BIT;
    case RenderGraphTextureUsage::ColorAttachment:
        return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    case RenderGraphTextureUsage::DepthAttachment:
        return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    case RenderGraphTextureUsage::TransferRead:
        return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case RenderGraphTextureUsage::TransferWrite:
        return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    throw std::runtime_error("render graph texture usage is invalid");
}

[[nodiscard]] VkBufferUsageFlags buffer_usage_flags(RenderGraphBufferUsage usage) {
    switch (usage) {
    case RenderGraphBufferUsage::UniformRead:
        return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case RenderGraphBufferUsage::StorageRead:
    case RenderGraphBufferUsage::StorageWrite:
    case RenderGraphBufferUsage::StorageReadWrite:
        return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case RenderGraphBufferUsage::VertexRead:
        return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case RenderGraphBufferUsage::IndexRead:
        return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case RenderGraphBufferUsage::TransferRead:
        return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    case RenderGraphBufferUsage::TransferWrite:
        return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    throw std::runtime_error("render graph buffer usage is invalid");
}

[[nodiscard]] VkImageUsageFlags transient_image_usage_flags(const CompiledRenderGraph& graph,
                                                            RenderGraphTextureHandle handle) {
    VkImageUsageFlags usage_flags = 0;
    for (const RenderGraphCompiledPass& pass : graph.passes()) {
        for (const RenderGraphTextureAccess& access : pass.texture_accesses) {
            if (access.handle == handle) {
                usage_flags |= image_usage_flags(access.usage);
            }
        }
    }
    return usage_flags;
}

[[nodiscard]] VkBufferUsageFlags transient_buffer_usage_flags(const CompiledRenderGraph& graph,
                                                              RenderGraphBufferHandle handle) {
    VkBufferUsageFlags usage_flags = 0;
    for (const RenderGraphCompiledPass& pass : graph.passes()) {
        for (const RenderGraphBufferAccess& access : pass.buffer_accesses) {
            if (access.handle == handle) {
                usage_flags |= buffer_usage_flags(access.usage);
            }
        }
    }
    return usage_flags;
}

[[nodiscard]] bool texture_desc_equal(const RenderGraphTextureDesc& lhs,
                                      const RenderGraphTextureDesc& rhs) {
    return lhs.label == rhs.label && lhs.extent.width == rhs.extent.width &&
           lhs.extent.height == rhs.extent.height && lhs.extent.depth == rhs.extent.depth &&
           lhs.format == rhs.format && lhs.aspects == rhs.aspects;
}

[[nodiscard]] bool buffer_desc_equal(const RenderGraphBufferDesc& lhs,
                                     const RenderGraphBufferDesc& rhs) {
    return lhs.label == rhs.label && lhs.byte_size == rhs.byte_size;
}

} // namespace

RenderGraphResourceSet::RenderGraphResourceSet(const CompiledRenderGraph& graph)
    : textures_(graph.textures().size()), buffers_(graph.buffers().size()) {
    capture_resource_keys(graph);
}

RenderGraphResourceSet::RenderGraphResourceSet(const cubey::vulkan::Device& device,
                                               const CompiledRenderGraph& graph)
    : RenderGraphResourceSet(graph) {
    allocate_transients(device, graph);
}

bool RenderGraphResourceSet::compatible(const CompiledRenderGraph& graph) const {
    std::vector<TextureResourceKey> texture_keys;
    texture_keys.reserve(graph.textures().size());
    for (const RenderGraphTextureResource& texture : graph.textures()) {
        texture_keys.push_back(TextureResourceKey{
            .lifetime = texture.lifetime,
            .desc = texture.desc,
            .usage_flags = transient_image_usage_flags(graph, texture.handle),
        });
    }

    std::vector<BufferResourceKey> buffer_keys;
    buffer_keys.reserve(graph.buffers().size());
    for (const RenderGraphBufferResource& buffer : graph.buffers()) {
        buffer_keys.push_back(BufferResourceKey{
            .lifetime = buffer.lifetime,
            .desc = buffer.desc,
            .usage_flags = transient_buffer_usage_flags(graph, buffer.handle),
        });
    }

    if (texture_keys.size() != texture_keys_.size() || buffer_keys.size() != buffer_keys_.size()) {
        return false;
    }
    for (std::size_t index = 0; index < texture_keys.size(); ++index) {
        const TextureResourceKey& lhs = texture_keys[index];
        const TextureResourceKey& rhs = texture_keys_[index];
        if (lhs.lifetime != rhs.lifetime || !texture_desc_equal(lhs.desc, rhs.desc) ||
            lhs.usage_flags != rhs.usage_flags) {
            return false;
        }
    }
    for (std::size_t index = 0; index < buffer_keys.size(); ++index) {
        const BufferResourceKey& lhs = buffer_keys[index];
        const BufferResourceKey& rhs = buffer_keys_[index];
        if (lhs.lifetime != rhs.lifetime || !buffer_desc_equal(lhs.desc, rhs.desc) ||
            lhs.usage_flags != rhs.usage_flags) {
            return false;
        }
    }
    return true;
}

void RenderGraphResourceSet::reset(const CompiledRenderGraph& graph) {
    if (!compatible(graph)) {
        throw std::runtime_error("render graph resource set cannot reset to incompatible graph");
    }
    textures_.assign(graph.textures().size(), std::nullopt);
    buffers_.assign(graph.buffers().size(), std::nullopt);
    bind_transient_resources();
}

void RenderGraphResourceSet::bind_texture(RenderGraphTextureHandle handle,
                                          RenderGraphResolvedTexture texture) {
    if (!handle || handle.index > textures_.size()) {
        throw std::runtime_error("render graph texture handle is invalid");
    }
    if (texture.image == VK_NULL_HANDLE || texture.view == VK_NULL_HANDLE) {
        throw std::runtime_error("render graph resolved texture requires image and view");
    }
    textures_[static_cast<std::size_t>(handle.index - 1U)] = texture;
}

void RenderGraphResourceSet::bind_buffer(RenderGraphBufferHandle handle,
                                         RenderGraphResolvedBuffer buffer) {
    if (!handle || handle.index > buffers_.size()) {
        throw std::runtime_error("render graph buffer handle is invalid");
    }
    if (buffer.buffer == VK_NULL_HANDLE || buffer.byte_size == 0) {
        throw std::runtime_error("render graph resolved buffer requires buffer and byte size");
    }
    const std::size_t index = static_cast<std::size_t>(handle.index - 1U);
    if (buffer.byte_size < buffer_keys_[index].desc.byte_size) {
        throw std::runtime_error("render graph resolved buffer is smaller than graph declaration");
    }
    buffers_[index] = buffer;
}

std::optional<RenderGraphResolvedTexture>
RenderGraphResourceSet::texture(RenderGraphTextureHandle handle) const {
    if (!handle || handle.index > textures_.size()) {
        throw std::runtime_error("render graph texture handle is invalid");
    }
    return textures_[static_cast<std::size_t>(handle.index - 1U)];
}

std::optional<RenderGraphResolvedBuffer>
RenderGraphResourceSet::buffer(RenderGraphBufferHandle handle) const {
    if (!handle || handle.index > buffers_.size()) {
        throw std::runtime_error("render graph buffer handle is invalid");
    }
    return buffers_[static_cast<std::size_t>(handle.index - 1U)];
}

void RenderGraphResourceSet::allocate_transients(const cubey::vulkan::Device& device,
                                                 const CompiledRenderGraph& graph) {
    for (const RenderGraphTextureResource& texture : graph.textures()) {
        if (texture.lifetime != RenderGraphResourceLifetime::Transient) {
            continue;
        }
        const VkImageUsageFlags usage_flags = transient_image_usage_flags(graph, texture.handle);
        if (usage_flags == 0) {
            continue;
        }
        transient_textures_.emplace_back(
            device,
            cubey::vulkan::ImageConfig{
                .extent = texture.desc.extent,
                .format = texture.desc.format,
                .usage = usage_flags,
                .aspect = texture.desc.aspects,
                .image_type = texture.desc.extent.depth > 1U ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
                .view_type =
                    texture.desc.extent.depth > 1U ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D,
            });
        const cubey::vulkan::Image& image = transient_textures_.back();
        const RenderGraphResolvedTexture resolved{
            .image = image.handle(),
            .view = image.view(),
        };
        transient_texture_bindings_.emplace_back(texture.handle, resolved);
        bind_texture(texture.handle, resolved);
    }

    for (const RenderGraphBufferResource& buffer : graph.buffers()) {
        if (buffer.lifetime != RenderGraphResourceLifetime::Transient) {
            continue;
        }
        const VkBufferUsageFlags usage_flags = transient_buffer_usage_flags(graph, buffer.handle);
        if (usage_flags == 0) {
            continue;
        }
        transient_buffers_.emplace_back(
            device, cubey::vulkan::BufferConfig{
                        .size = buffer.desc.byte_size,
                        .usage = usage_flags,
                        .memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    });
        const cubey::vulkan::Buffer& vk_buffer = transient_buffers_.back();
        const RenderGraphResolvedBuffer resolved{
            .buffer = vk_buffer.handle(),
            .byte_size = vk_buffer.size(),
        };
        transient_buffer_bindings_.emplace_back(buffer.handle, resolved);
        bind_buffer(buffer.handle, resolved);
    }
}

void RenderGraphResourceSet::capture_resource_keys(const CompiledRenderGraph& graph) {
    texture_keys_.clear();
    texture_keys_.reserve(graph.textures().size());
    for (const RenderGraphTextureResource& texture : graph.textures()) {
        texture_keys_.push_back(TextureResourceKey{
            .lifetime = texture.lifetime,
            .desc = texture.desc,
            .usage_flags = transient_image_usage_flags(graph, texture.handle),
        });
    }

    buffer_keys_.clear();
    buffer_keys_.reserve(graph.buffers().size());
    for (const RenderGraphBufferResource& buffer : graph.buffers()) {
        buffer_keys_.push_back(BufferResourceKey{
            .lifetime = buffer.lifetime,
            .desc = buffer.desc,
            .usage_flags = transient_buffer_usage_flags(graph, buffer.handle),
        });
    }
}

void RenderGraphResourceSet::bind_transient_resources() {
    for (const auto& [handle, resolved] : transient_texture_bindings_) {
        bind_texture(handle, resolved);
    }
    for (const auto& [handle, resolved] : transient_buffer_bindings_) {
        bind_buffer(handle, resolved);
    }
}

} // namespace cubey::render
