#include <cubey/render/render_graph.h>

#include <span>
#include <stdexcept>
#include <utility>

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

CompiledRenderGraph::CompiledRenderGraph(std::vector<RenderGraphTextureResource> textures,
                                         std::vector<RenderGraphBufferResource> buffers,
                                         std::vector<RenderGraphCompiledPass> passes)
    : textures_(std::move(textures)), buffers_(std::move(buffers)), passes_(std::move(passes)) {}

RenderGraphExecutionContext::RenderGraphExecutionContext(const CompiledRenderGraph& graph,
                                                         std::size_t pass_index,
                                                         const RenderGraphResourceSet* resources)
    : graph_(&graph), resources_(resources), pass_index_(pass_index) {}

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
    execute(nullptr);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet& resources) const {
    execute(&resources);
}

void CompiledRenderGraph::execute(const RenderGraphResourceSet* resources) const {
    for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        const RenderGraphCompiledPass& pass = passes_[pass_index];
        if (!pass.execute) {
            throw std::runtime_error("render graph pass has no execute callback");
        }
        pass.execute(RenderGraphExecutionContext(*this, pass_index, resources));
    }
}

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
            device, cubey::vulkan::ImageConfig{
                        .extent = {texture.desc.extent.width, texture.desc.extent.height, 1},
                        .format = texture.desc.format,
                        .usage = usage_flags,
                        .aspect = texture.desc.aspects,
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

void record_render_graph_barriers(const cubey::vulkan::CommandRecorder& recorder,
                                  const RenderGraphExecutionContext& context,
                                  RenderGraphBarrierPhase phase) {
    const RenderGraphCompiledPass& pass = context.pass();
    const std::vector<RenderGraphTextureBarrier>& texture_barriers =
        phase == RenderGraphBarrierPhase::BeforePass ? pass.before_texture_barriers
                                                     : pass.after_texture_barriers;
    const std::vector<RenderGraphBufferBarrier>& buffer_barriers =
        phase == RenderGraphBarrierPhase::BeforePass ? pass.before_buffer_barriers
                                                     : pass.after_buffer_barriers;

    for (const RenderGraphTextureBarrier& barrier : texture_barriers) {
        const RenderGraphTextureResource& resource = context.texture(barrier.handle);
        const RenderGraphResolvedTexture resolved = context.resolved_texture(barrier.handle);
        if (resolved.image == VK_NULL_HANDLE) {
            throw std::runtime_error("render graph texture barrier requires an allocated texture");
        }
        const auto transition = cubey::vulkan::ImageLayoutTransition{
            .image = resolved.image,
            .aspect_mask = resource.desc.aspects,
            .old_layout = barrier.source_state.layout,
            .new_layout = barrier.destination_state.layout,
            .src_access_mask = barrier.source_state.access_mask,
            .dst_access_mask = barrier.destination_state.access_mask,
            .src_stage_mask = barrier.source_state.stage_mask,
            .dst_stage_mask = barrier.destination_state.stage_mask,
        };
        const VkImageMemoryBarrier image_barrier = cubey::vulkan::image_memory_barrier(transition);
        recorder.pipeline_barrier(transition.src_stage_mask, transition.dst_stage_mask, 0, {}, {},
                                  std::span<const VkImageMemoryBarrier>(&image_barrier, 1));
    }

    for (const RenderGraphBufferBarrier& barrier : buffer_barriers) {
        const RenderGraphResolvedBuffer resolved = context.resolved_buffer(barrier.handle);
        if (resolved.buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("render graph buffer barrier requires an allocated buffer");
        }
        auto vk_barrier = VkBufferMemoryBarrier{};
        vk_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        vk_barrier.srcAccessMask = barrier.source_state.access_mask;
        vk_barrier.dstAccessMask = barrier.destination_state.access_mask;
        vk_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_barrier.buffer = resolved.buffer;
        vk_barrier.offset = 0;
        vk_barrier.size = resolved.byte_size;
        recorder.pipeline_barrier(barrier.source_state.stage_mask,
                                  barrier.destination_state.stage_mask, 0, {},
                                  std::span<const VkBufferMemoryBarrier>(&vk_barrier, 1), {});
    }
}

ColorTargetView resolved_color_target_view(const RenderGraphExecutionContext& context,
                                           RenderGraphTextureHandle handle) {
    const RenderGraphTextureResource& resource = context.texture(handle);
    if (resource.desc.aspects != VK_IMAGE_ASPECT_COLOR_BIT) {
        throw std::runtime_error("render graph color target view requires a color texture");
    }
    const RenderGraphResolvedTexture resolved = context.resolved_texture(handle);
    return color_target_view(resource.desc.extent, resource.desc.format, resolved.image,
                             resolved.view);
}

} // namespace cubey::render
