#pragma once

#include <cubey/render/render_graph_types.h>

#include <cstdint>
#include <stdexcept>

namespace cubey::render::detail {

enum class RenderGraphQueueDomainMask : std::uint8_t {
    None = 0,
    Graphics = 1U << 0U,
    Compute = 1U << 1U,
    Transfer = 1U << 2U,
    GraphicsCompute = static_cast<std::uint8_t>(Graphics) | static_cast<std::uint8_t>(Compute),
    Any = static_cast<std::uint8_t>(GraphicsCompute) | static_cast<std::uint8_t>(Transfer),
};

struct RenderGraphTextureUsageTraits {
    bool reads = false;
    bool writes = false;
    bool shader_access = false;
    bool transfer_access = false;
    bool attachment_access = false;
    RenderGraphQueueDomainMask allowed_domains = RenderGraphQueueDomainMask::None;
    VkImageAspectFlags allowed_aspects = 0;
    VkImageUsageFlags image_usage_flags = 0;
};

struct RenderGraphBufferUsageTraits {
    bool reads = false;
    bool writes = false;
    bool shader_access = false;
    bool transfer_access = false;
    bool vertex_input_access = false;
    RenderGraphQueueDomainMask allowed_domains = RenderGraphQueueDomainMask::None;
    VkBufferUsageFlags buffer_usage_flags = 0;
};

[[nodiscard]] inline RenderGraphQueueDomainMask
render_graph_queue_domain_mask(RenderGraphQueueDomain domain) {
    switch (domain) {
    case RenderGraphQueueDomain::Graphics:
        return RenderGraphQueueDomainMask::Graphics;
    case RenderGraphQueueDomain::Compute:
        return RenderGraphQueueDomainMask::Compute;
    case RenderGraphQueueDomain::Transfer:
        return RenderGraphQueueDomainMask::Transfer;
    }
    throw std::runtime_error("render graph queue domain is invalid");
}

[[nodiscard]] inline bool
render_graph_usage_allows_queue(RenderGraphQueueDomainMask allowed_domains,
                                RenderGraphQueueDomain domain) {
    const auto allowed = static_cast<std::uint8_t>(allowed_domains);
    const auto requested = static_cast<std::uint8_t>(render_graph_queue_domain_mask(domain));
    return (allowed & requested) != 0U;
}

[[nodiscard]] inline const RenderGraphTextureUsageTraits&
render_graph_texture_usage_traits(RenderGraphTextureUsage usage) {
    static constexpr RenderGraphTextureUsageTraits sampled_read{
        .reads = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits storage_read{
        .reads = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_STORAGE_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits storage_write{
        .writes = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_STORAGE_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits storage_read_write{
        .reads = true,
        .writes = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_STORAGE_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits color_attachment{
        .writes = true,
        .attachment_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Graphics,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits color_attachment_read_write{
        .reads = true,
        .writes = true,
        .attachment_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Graphics,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits depth_attachment{
        .reads = true,
        .writes = true,
        .attachment_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Graphics,
        .allowed_aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits transfer_read{
        .reads = true,
        .transfer_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Any,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    };
    static constexpr RenderGraphTextureUsageTraits transfer_write{
        .writes = true,
        .transfer_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Any,
        .allowed_aspects = VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT,
        .image_usage_flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };

    switch (usage) {
    case RenderGraphTextureUsage::SampledRead:
        return sampled_read;
    case RenderGraphTextureUsage::StorageRead:
        return storage_read;
    case RenderGraphTextureUsage::StorageWrite:
        return storage_write;
    case RenderGraphTextureUsage::StorageReadWrite:
        return storage_read_write;
    case RenderGraphTextureUsage::ColorAttachment:
        return color_attachment;
    case RenderGraphTextureUsage::ColorAttachmentReadWrite:
        return color_attachment_read_write;
    case RenderGraphTextureUsage::DepthAttachment:
        return depth_attachment;
    case RenderGraphTextureUsage::TransferRead:
        return transfer_read;
    case RenderGraphTextureUsage::TransferWrite:
        return transfer_write;
    }
    throw std::runtime_error("render graph texture usage is invalid");
}

[[nodiscard]] inline bool
render_graph_texture_usage_allows_aspects(const RenderGraphTextureUsageTraits& traits,
                                          VkImageAspectFlags aspects) {
    return (traits.allowed_aspects & aspects) == aspects;
}

[[nodiscard]] inline const RenderGraphBufferUsageTraits&
render_graph_buffer_usage_traits(RenderGraphBufferUsage usage) {
    static constexpr RenderGraphBufferUsageTraits uniform_read{
        .reads = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .buffer_usage_flags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };
    static constexpr RenderGraphBufferUsageTraits storage_read{
        .reads = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    static constexpr RenderGraphBufferUsageTraits storage_write{
        .writes = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    static constexpr RenderGraphBufferUsageTraits storage_read_write{
        .reads = true,
        .writes = true,
        .shader_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::GraphicsCompute,
        .buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    static constexpr RenderGraphBufferUsageTraits vertex_read{
        .reads = true,
        .vertex_input_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Graphics,
        .buffer_usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    };
    static constexpr RenderGraphBufferUsageTraits index_read{
        .reads = true,
        .vertex_input_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Graphics,
        .buffer_usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    };
    static constexpr RenderGraphBufferUsageTraits transfer_read{
        .reads = true,
        .transfer_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Any,
        .buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    static constexpr RenderGraphBufferUsageTraits transfer_write{
        .writes = true,
        .transfer_access = true,
        .allowed_domains = RenderGraphQueueDomainMask::Any,
        .buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };

    switch (usage) {
    case RenderGraphBufferUsage::UniformRead:
        return uniform_read;
    case RenderGraphBufferUsage::StorageRead:
        return storage_read;
    case RenderGraphBufferUsage::StorageWrite:
        return storage_write;
    case RenderGraphBufferUsage::StorageReadWrite:
        return storage_read_write;
    case RenderGraphBufferUsage::VertexRead:
        return vertex_read;
    case RenderGraphBufferUsage::IndexRead:
        return index_read;
    case RenderGraphBufferUsage::TransferRead:
        return transfer_read;
    case RenderGraphBufferUsage::TransferWrite:
        return transfer_write;
    }
    throw std::runtime_error("render graph buffer usage is invalid");
}

} // namespace cubey::render::detail
