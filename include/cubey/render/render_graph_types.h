#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace cubey::render {

struct RenderGraphTextureHandle {
    std::uint32_t index = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(RenderGraphTextureHandle lhs, RenderGraphTextureHandle rhs) = default;
};

struct RenderGraphBufferHandle {
    std::uint32_t index = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(RenderGraphBufferHandle lhs, RenderGraphBufferHandle rhs) = default;
};

enum class RenderGraphResourceLifetime : std::uint8_t {
    Imported,
    Transient,
};

enum class RenderGraphQueueDomain : std::uint8_t {
    Graphics,
    Compute,
    Transfer,
};

enum class RenderGraphTextureUsage : std::uint8_t {
    SampledRead,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    ColorAttachment,
    DepthAttachment,
    TransferRead,
    TransferWrite,
};

enum class RenderGraphBufferUsage : std::uint8_t {
    UniformRead,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    VertexRead,
    IndexRead,
    TransferRead,
    TransferWrite,
};

enum class RenderGraphBarrierPhase : std::uint8_t {
    BeforePass,
    AfterPass,
};

struct RenderGraphTextureState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags access_mask = 0;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    friend bool operator==(RenderGraphTextureState lhs, RenderGraphTextureState rhs) = default;
};

struct RenderGraphBufferState {
    VkAccessFlags access_mask = 0;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    friend bool operator==(RenderGraphBufferState lhs, RenderGraphBufferState rhs) = default;
};

[[nodiscard]] RenderGraphTextureState render_graph_undefined_texture_state() noexcept;
[[nodiscard]] RenderGraphTextureState render_graph_present_texture_state() noexcept;
[[nodiscard]] RenderGraphTextureState render_graph_color_attachment_texture_state() noexcept;
[[nodiscard]] RenderGraphTextureState render_graph_sampled_depth_texture_state() noexcept;

struct RenderGraphTextureDesc {
    std::string label{};
    VkExtent3D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspects = 0;
};

struct RenderGraphBufferDesc {
    std::string label{};
    VkDeviceSize byte_size = 0;
};

struct RenderGraphTextureResource {
    RenderGraphTextureHandle handle{};
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
    RenderGraphTextureDesc desc{};
    VkImage imported_image = VK_NULL_HANDLE;
    VkImageView imported_view = VK_NULL_HANDLE;
    std::optional<RenderGraphTextureState> initial_state{};
    std::optional<RenderGraphTextureState> final_state{};
};

struct RenderGraphBufferResource {
    RenderGraphBufferHandle handle{};
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
    RenderGraphBufferDesc desc{};
    VkBuffer imported_buffer = VK_NULL_HANDLE;
    std::optional<RenderGraphBufferState> initial_state{};
    std::optional<RenderGraphBufferState> final_state{};
};

struct RenderGraphTextureAccess {
    RenderGraphTextureHandle handle{};
    RenderGraphTextureUsage usage = RenderGraphTextureUsage::SampledRead;
    VkPipelineStageFlags stage_mask = 0;
};

struct RenderGraphBufferAccess {
    RenderGraphBufferHandle handle{};
    RenderGraphBufferUsage usage = RenderGraphBufferUsage::UniformRead;
    VkPipelineStageFlags stage_mask = 0;
};

struct RenderGraphTextureBarrier {
    RenderGraphTextureHandle handle{};
    std::optional<std::size_t> source_pass_index{};
    std::optional<RenderGraphTextureUsage> source_usage{};
    std::optional<RenderGraphTextureUsage> destination_usage{};
    RenderGraphTextureState source_state{};
    RenderGraphTextureState destination_state{};
};

struct RenderGraphBufferBarrier {
    RenderGraphBufferHandle handle{};
    std::optional<std::size_t> source_pass_index{};
    std::optional<RenderGraphBufferUsage> source_usage{};
    std::optional<RenderGraphBufferUsage> destination_usage{};
    RenderGraphBufferState source_state{};
    RenderGraphBufferState destination_state{};
};

struct RenderGraphResolvedTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct RenderGraphSampledTextureView {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct RenderGraphResolvedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize byte_size = 0;
};

} // namespace cubey::render
