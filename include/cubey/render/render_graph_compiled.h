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
};

} // namespace cubey::render
