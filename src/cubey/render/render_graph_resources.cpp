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

} // namespace

RenderGraphResourceSet::RenderGraphResourceSet(const CompiledRenderGraph& graph)
    : textures_(graph.textures().size()), buffers_(graph.buffers().size()) {}

RenderGraphResourceSet::RenderGraphResourceSet(const cubey::vulkan::Device& device,
                                               const CompiledRenderGraph& graph)
    : RenderGraphResourceSet(graph) {
    allocate_transients(device, graph);
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
    buffers_[static_cast<std::size_t>(handle.index - 1U)] = buffer;
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
        bind_texture(texture.handle, RenderGraphResolvedTexture{
                                         .image = image.handle(),
                                         .view = image.view(),
                                     });
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
        bind_buffer(buffer.handle, RenderGraphResolvedBuffer{
                                       .buffer = vk_buffer.handle(),
                                       .byte_size = vk_buffer.size(),
                                   });
    }
}

} // namespace cubey::render
