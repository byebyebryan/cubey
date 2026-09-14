#pragma once

#include <cubey/render/material.h>
#include <cubey/render/render_graph_types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cubey::vulkan {
class CommandRecorder;
class GpuTimestampProfiler;
} // namespace cubey::vulkan

namespace cubey::render {

class CompiledRenderGraph;
class RenderGraphResourceSet;
struct RenderGraphCompiledPass;

class RenderGraphExecutionContext {
  public:
    [[nodiscard]] const CompiledRenderGraph& graph() const;
    [[nodiscard]] const RenderGraphCompiledPass& pass() const;
    [[nodiscard]] const cubey::vulkan::CommandRecorder& recorder() const;
    [[nodiscard]] std::size_t pass_index() const noexcept {
        return pass_index_;
    }
    [[nodiscard]] const RenderGraphTextureResource& texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource& buffer(RenderGraphBufferHandle handle) const;
    [[nodiscard]] RenderGraphResolvedTexture
    resolved_texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] RenderGraphResolvedBuffer resolved_buffer(RenderGraphBufferHandle handle) const;

  private:
    friend class CompiledRenderGraph;

    RenderGraphExecutionContext(const CompiledRenderGraph& graph, std::size_t pass_index,
                                const RenderGraphResourceSet* resources,
                                const cubey::vulkan::CommandRecorder* recorder);

    const CompiledRenderGraph* graph_ = nullptr;
    const RenderGraphResourceSet* resources_ = nullptr;
    const cubey::vulkan::CommandRecorder* recorder_ = nullptr;
    std::size_t pass_index_ = 0;
};

using RenderGraphExecuteCallback = std::function<void(const RenderGraphExecutionContext&)>;

struct RenderGraphCompiledPass {
    std::string label{};
    RenderGraphQueueDomain queue_domain = RenderGraphQueueDomain::Graphics;
    std::vector<RenderGraphTextureAccess> texture_accesses{};
    std::vector<RenderGraphBufferAccess> buffer_accesses{};
    std::vector<RenderGraphTextureBarrier> before_texture_barriers{};
    std::vector<RenderGraphBufferBarrier> before_buffer_barriers{};
    std::vector<RenderGraphTextureBarrier> after_texture_barriers{};
    std::vector<RenderGraphBufferBarrier> after_buffer_barriers{};
    std::optional<MaterialPassInfo> material_pass{};
    RenderGraphExecuteCallback execute{};
};

// Structural counts derived from the immutable compiled graph. These values
// describe the graph declaration and synchronization work without retaining
// any profiler or execution state. CompiledRenderGraph::metrics() computes a
// caller-owned value on demand.
struct RenderGraphCompiledMetrics {
    std::size_t pass_count = 0;
    std::size_t texture_count = 0;
    std::size_t buffer_count = 0;
    std::size_t before_texture_barrier_count = 0;
    std::size_t before_buffer_barrier_count = 0;
    std::size_t after_texture_barrier_count = 0;
    std::size_t after_buffer_barrier_count = 0;
    std::size_t before_barrier_count = 0;
    std::size_t after_barrier_count = 0;
};

// Allocation-relevant requirements derived from immutable graph declarations.
// Labels, imported Vulkan handles, and imported synchronization state remain
// graph diagnostics/execution data rather than resource-set identity.
struct RenderGraphTextureRequirement {
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
    VkExtent3D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspects = 0;
    VkImageUsageFlags usage_flags = 0;

    friend bool operator==(RenderGraphTextureRequirement lhs,
                           RenderGraphTextureRequirement rhs) noexcept {
        return lhs.lifetime == rhs.lifetime && lhs.extent.width == rhs.extent.width &&
               lhs.extent.height == rhs.extent.height && lhs.extent.depth == rhs.extent.depth &&
               lhs.format == rhs.format && lhs.aspects == rhs.aspects &&
               lhs.usage_flags == rhs.usage_flags;
    }
};

struct RenderGraphBufferRequirement {
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;
    VkDeviceSize byte_size = 0;
    VkBufferUsageFlags usage_flags = 0;

    friend bool operator==(RenderGraphBufferRequirement lhs,
                           RenderGraphBufferRequirement rhs) = default;
};

struct RenderGraphResourceSignature {
    std::vector<RenderGraphTextureRequirement> textures{};
    std::vector<RenderGraphBufferRequirement> buffers{};

    friend bool operator==(const RenderGraphResourceSignature& lhs,
                           const RenderGraphResourceSignature& rhs) = default;
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

    // The signature is computed once with graph compilation and records only
    // physical resource requirements used to allocate/reuse resource sets.
    [[nodiscard]] const RenderGraphResourceSignature& resource_signature() const noexcept {
        return resource_signature_;
    }

    [[nodiscard]] RenderGraphCompiledMetrics metrics() const noexcept;

    [[nodiscard]] const RenderGraphTextureResource& texture(RenderGraphTextureHandle handle) const;
    [[nodiscard]] const RenderGraphBufferResource& buffer(RenderGraphBufferHandle handle) const;
    void execute() const;
    void execute(const RenderGraphResourceSet& resources) const;
    void execute(const RenderGraphResourceSet& resources,
                 const cubey::vulkan::CommandRecorder& recorder) const;
    void execute(const RenderGraphResourceSet& resources,
                 const cubey::vulkan::CommandRecorder& recorder,
                 cubey::vulkan::GpuTimestampProfiler* profiler,
                 std::uint32_t frame_slot_index) const;

  private:
    void execute(const RenderGraphResourceSet* resources,
                 const cubey::vulkan::CommandRecorder* recorder,
                 cubey::vulkan::GpuTimestampProfiler* profiler,
                 std::uint32_t frame_slot_index) const;

    std::vector<RenderGraphTextureResource> textures_{};
    std::vector<RenderGraphBufferResource> buffers_{};
    std::vector<RenderGraphCompiledPass> passes_{};
    RenderGraphResourceSignature resource_signature_{};
};

} // namespace cubey::render
