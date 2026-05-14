#include <cubey/render/render_graph.h>

#include <stdexcept>
#include <utility>

namespace cubey::render {

CompiledRenderGraph::CompiledRenderGraph(std::vector<RenderGraphTextureResource> textures,
                                         std::vector<RenderGraphBufferResource> buffers,
                                         std::vector<RenderGraphCompiledPass> passes)
    : textures_(std::move(textures)), buffers_(std::move(buffers)), passes_(std::move(passes)) {}

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
    const RenderGraphTextureResource& resource = texture(handle);
    if (resources_ != nullptr) {
        const std::optional<RenderGraphResolvedTexture> resolved = resources_->texture(handle);
        if (resolved.has_value()) {
            return resolved.value();
        }
    }
    if (resource.lifetime == RenderGraphResourceLifetime::Imported) {
        return {
            .image = resource.imported_image,
            .view = resource.imported_view,
        };
    }
    throw std::runtime_error("render graph texture requires a resolved resource");
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
    execute(nullptr, nullptr);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet& resources) const {
    execute(&resources, nullptr);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet& resources,
                                  const cubey::vulkan::CommandRecorder& recorder) const {
    execute(&resources, &recorder);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet* resources,
                                  const cubey::vulkan::CommandRecorder* recorder) const {
    if (recorder != nullptr && resources == nullptr) {
        throw std::runtime_error("recorder-backed render graph execution requires resources");
    }

    for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        const RenderGraphCompiledPass& pass = passes_[pass_index];
        if (!pass.execute) {
            throw std::runtime_error("render graph pass has no execute callback");
        }
        const RenderGraphExecutionContext context(*this, pass_index, resources, recorder);
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
