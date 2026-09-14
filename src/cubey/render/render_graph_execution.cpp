#include <cubey/render/render_graph.h>

#include "render_graph_private.h"

#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <stdexcept>
#include <string_view>
#include <utility>

namespace cubey::render {

CompiledRenderGraph::CompiledRenderGraph(std::vector<RenderGraphTextureResource> textures,
                                         std::vector<RenderGraphBufferResource> buffers,
                                         std::vector<RenderGraphCompiledPass> passes)
    : textures_(std::move(textures)), buffers_(std::move(buffers)), passes_(std::move(passes)) {}

RenderGraphCompiledMetrics CompiledRenderGraph::metrics() const noexcept {
    RenderGraphCompiledMetrics result{
        .pass_count = passes_.size(),
        .texture_count = textures_.size(),
        .buffer_count = buffers_.size(),
    };
    for (const RenderGraphCompiledPass& pass : passes_) {
        result.before_texture_barrier_count += pass.before_texture_barriers.size();
        result.before_buffer_barrier_count += pass.before_buffer_barriers.size();
        result.after_texture_barrier_count += pass.after_texture_barriers.size();
        result.after_buffer_barrier_count += pass.after_buffer_barriers.size();
    }
    result.before_barrier_count =
        result.before_texture_barrier_count + result.before_buffer_barrier_count;
    result.after_barrier_count =
        result.after_texture_barrier_count + result.after_buffer_barrier_count;
    return result;
}

RenderGraphExecutionContext::RenderGraphExecutionContext(
    const CompiledRenderGraph& graph, std::size_t pass_index,
    const RenderGraphResourceSet* resources, const cubey::vulkan::CommandRecorder* recorder)
    : graph_(&graph), resources_(resources), recorder_(recorder), pass_index_(pass_index) {}

const CompiledRenderGraph& RenderGraphExecutionContext::graph() const {
    if (graph_ == nullptr) {
        throw std::runtime_error("render graph execution context is not initialized");
    }
    return *graph_;
}

const RenderGraphCompiledPass& RenderGraphExecutionContext::pass() const {
    const CompiledRenderGraph& compiled = graph();
    if (pass_index_ >= compiled.passes().size()) {
        throw std::runtime_error("render graph execution pass index is invalid");
    }
    return compiled.passes()[pass_index_];
}

const cubey::vulkan::CommandRecorder& RenderGraphExecutionContext::recorder() const {
    if (recorder_ == nullptr) {
        throw std::runtime_error("render graph execution context has no command recorder");
    }
    return *recorder_;
}

const RenderGraphTextureResource&
RenderGraphExecutionContext::texture(RenderGraphTextureHandle handle) const {
    return graph().texture(handle);
}

const RenderGraphBufferResource&
RenderGraphExecutionContext::buffer(RenderGraphBufferHandle handle) const {
    return graph().buffer(handle);
}

RenderGraphResolvedTexture
RenderGraphExecutionContext::resolved_texture(RenderGraphTextureHandle handle) const {
    return detail::render_graph_resolved_texture(graph(), resources_, handle);
}

RenderGraphResolvedBuffer
RenderGraphExecutionContext::resolved_buffer(RenderGraphBufferHandle handle) const {
    const RenderGraphBufferResource& resource = buffer(handle);
    if (resources_ != nullptr) {
        const std::optional<RenderGraphResolvedBuffer> resolved = resources_->buffer(handle);
        if (resolved.has_value()) {
            return resolved.value();
        }
    }
    if (resource.lifetime == RenderGraphResourceLifetime::Imported) {
        return {
            .buffer = resource.imported_buffer,
            .byte_size = resource.desc.byte_size,
        };
    }
    throw std::runtime_error("render graph buffer requires a resolved resource");
}

const RenderGraphTextureResource&
CompiledRenderGraph::texture(RenderGraphTextureHandle handle) const {
    if (!handle || handle.index > textures_.size()) {
        throw std::runtime_error("render graph texture handle is invalid");
    }
    return textures_[static_cast<std::size_t>(handle.index - 1U)];
}

const RenderGraphBufferResource& CompiledRenderGraph::buffer(RenderGraphBufferHandle handle) const {
    if (!handle || handle.index > buffers_.size()) {
        throw std::runtime_error("render graph buffer handle is invalid");
    }
    return buffers_[static_cast<std::size_t>(handle.index - 1U)];
}

void CompiledRenderGraph::execute() const {
    execute(nullptr, nullptr, nullptr, 0);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet& resources) const {
    execute(&resources, nullptr, nullptr, 0);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet& resources,
                                  const cubey::vulkan::CommandRecorder& recorder) const {
    execute(&resources, &recorder, nullptr, 0);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet& resources,
                                  const cubey::vulkan::CommandRecorder& recorder,
                                  cubey::vulkan::GpuTimestampProfiler* profiler,
                                  std::uint32_t frame_slot_index) const {
    execute(&resources, &recorder, profiler, frame_slot_index);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet* resources,
                                  const cubey::vulkan::CommandRecorder* recorder,
                                  cubey::vulkan::GpuTimestampProfiler* profiler,
                                  std::uint32_t frame_slot_index) const {
    if (resources != nullptr && !resources->compatible(*this)) {
        throw std::runtime_error("render graph resource set is incompatible with graph");
    }
    if (recorder != nullptr && resources == nullptr) {
        throw std::runtime_error("recorder-backed render graph execution requires resources");
    }
    if (profiler != nullptr && recorder == nullptr) {
        throw std::runtime_error("profiled render graph execution requires a recorder");
    }

    for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        const RenderGraphCompiledPass& pass = passes_[pass_index];
        if (!pass.execute) {
            throw std::runtime_error("render graph pass has no execute callback");
        }
        const RenderGraphExecutionContext context(*this, pass_index, resources, recorder);
        const std::string_view profile_label =
            pass.label.empty() ? std::string_view("render graph pass")
                               : std::string_view(pass.label);
        cubey::vulkan::GpuTimestampScope profile_scope(
            profiler, recorder != nullptr ? recorder->handle() : VK_NULL_HANDLE, frame_slot_index,
            profile_label);
        if (recorder != nullptr) {
            record_render_graph_barriers(*recorder, context, RenderGraphBarrierPhase::BeforePass);
        }
        pass.execute(context);
        if (recorder != nullptr) {
            record_render_graph_barriers(*recorder, context, RenderGraphBarrierPhase::AfterPass);
        }
    }
}

} // namespace cubey::render
