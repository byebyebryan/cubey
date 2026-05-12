#pragma once

#include <cubey/render/target.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cubey::render {

struct RenderGraphTextureHandle {
    std::uint32_t index = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(RenderGraphTextureHandle lhs,
                           RenderGraphTextureHandle rhs) = default;
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
    ColorAttachment,
    DepthAttachment,
    TransferRead,
    TransferWrite,
};

enum class RenderGraphBufferUsage : std::uint8_t {
    UniformRead,
    StorageRead,
    StorageWrite,
    TransferRead,
    TransferWrite,
};

struct RenderGraphTextureDesc {
    std::string label{};
    VkExtent2D extent{};
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
};

struct RenderGraphBufferResource {
    RenderGraphBufferHandle handle{};
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
    RenderGraphBufferDesc desc{};
    VkBuffer imported_buffer = VK_NULL_HANDLE;
};

struct RenderGraphTextureAccess {
    RenderGraphTextureHandle handle{};
    RenderGraphTextureUsage usage = RenderGraphTextureUsage::SampledRead;
};

struct RenderGraphBufferAccess {
    RenderGraphBufferHandle handle{};
    RenderGraphBufferUsage usage = RenderGraphBufferUsage::UniformRead;
};

class CompiledRenderGraph;
struct RenderGraphCompiledPass;

class RenderGraphExecutionContext {
  public:
    [[nodiscard]] const CompiledRenderGraph& graph() const;
    [[nodiscard]] const RenderGraphCompiledPass& pass() const;
    [[nodiscard]] std::size_t pass_index() const noexcept {
        return pass_index_;
    }
    [[nodiscard]] const RenderGraphTextureResource&
    texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource& buffer(RenderGraphBufferHandle handle) const;

  private:
    friend class CompiledRenderGraph;

    RenderGraphExecutionContext(const CompiledRenderGraph& graph, std::size_t pass_index);

    const CompiledRenderGraph* graph_ = nullptr;
    std::size_t pass_index_ = 0;
};

using RenderGraphExecuteCallback = std::function<void(const RenderGraphExecutionContext&)>;

struct RenderGraphCompiledPass {
    std::string label{};
    RenderGraphQueueDomain queue_domain = RenderGraphQueueDomain::Graphics;
    std::vector<RenderGraphTextureAccess> texture_accesses{};
    std::vector<RenderGraphBufferAccess> buffer_accesses{};
    RenderGraphExecuteCallback execute{};
};

class CompiledRenderGraph {
  public:
    CompiledRenderGraph() = default;
    CompiledRenderGraph(std::vector<RenderGraphTextureResource> textures,
                        std::vector<RenderGraphBufferResource> buffers,
                        std::vector<RenderGraphCompiledPass> passes);

    [[nodiscard]] const std::vector<RenderGraphTextureResource>& textures() const noexcept {
        return textures_;
    }

    [[nodiscard]] const std::vector<RenderGraphBufferResource>& buffers() const noexcept {
        return buffers_;
    }

    [[nodiscard]] const std::vector<RenderGraphCompiledPass>& passes() const noexcept {
        return passes_;
    }

    [[nodiscard]] const RenderGraphTextureResource&
    texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource& buffer(RenderGraphBufferHandle handle) const;
    void execute() const;

  private:
    std::vector<RenderGraphTextureResource> textures_{};
    std::vector<RenderGraphBufferResource> buffers_{};
    std::vector<RenderGraphCompiledPass> passes_{};
};

class RenderGraphBuilder;

class RenderGraphPassBuilder {
  public:
    RenderGraphPassBuilder& read_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& read_storage_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& write_storage_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& write_color(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& write_depth(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& transfer_read_texture(RenderGraphTextureHandle handle);
    RenderGraphPassBuilder& transfer_write_texture(RenderGraphTextureHandle handle);

    RenderGraphPassBuilder& read_uniform_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& read_storage_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& write_storage_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& transfer_read_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& transfer_write_buffer(RenderGraphBufferHandle handle);
    RenderGraphPassBuilder& execute(RenderGraphExecuteCallback callback);

  private:
    friend class RenderGraphBuilder;

    RenderGraphPassBuilder(RenderGraphBuilder& graph, std::uint32_t pass_index);

    RenderGraphBuilder* graph_ = nullptr;
    std::uint32_t pass_index_ = 0;
};

class RenderGraphBuilder {
  public:
    [[nodiscard]] RenderGraphTextureHandle import_color_target(std::string label,
                                                               ColorTargetView target);
    [[nodiscard]] RenderGraphTextureHandle import_depth_target(std::string label,
                                                               DepthTargetView target);
    [[nodiscard]] RenderGraphTextureHandle import_texture(RenderGraphTextureDesc desc,
                                                          VkImage image, VkImageView view);
    [[nodiscard]] RenderGraphTextureHandle create_texture(RenderGraphTextureDesc desc);

    [[nodiscard]] RenderGraphBufferHandle import_buffer(RenderGraphBufferDesc desc,
                                                        VkBuffer buffer);
    [[nodiscard]] RenderGraphBufferHandle create_buffer(RenderGraphBufferDesc desc);

    [[nodiscard]] RenderGraphPassBuilder add_pass(std::string label,
                                                  RenderGraphQueueDomain domain);
    [[nodiscard]] CompiledRenderGraph compile() const;

  private:
    friend class RenderGraphPassBuilder;

    void add_texture_access(std::uint32_t pass_index, RenderGraphTextureHandle handle,
                            RenderGraphTextureUsage usage);
    void add_buffer_access(std::uint32_t pass_index, RenderGraphBufferHandle handle,
                           RenderGraphBufferUsage usage);
    void set_execute_callback(std::uint32_t pass_index, RenderGraphExecuteCallback callback);

    [[nodiscard]] const RenderGraphTextureResource&
    texture_resource(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource&
    buffer_resource(RenderGraphBufferHandle handle) const;

    std::vector<RenderGraphTextureResource> textures_{};
    std::vector<RenderGraphBufferResource> buffers_{};
    std::vector<RenderGraphCompiledPass> passes_{};
};

} // namespace cubey::render
