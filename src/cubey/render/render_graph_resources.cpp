#include <cubey/render/render_graph.h>

#include <stdexcept>

namespace cubey::render {

RenderGraphResourceSet::RenderGraphResourceSet(const CompiledRenderGraph& graph)
    : textures_(graph.textures().size()), buffers_(graph.buffers().size()),
      resource_signature_(graph.resource_signature()) {}

RenderGraphResourceSet::RenderGraphResourceSet(const cubey::vulkan::Device& device,
                                               const CompiledRenderGraph& graph)
    : RenderGraphResourceSet(graph) {
    allocate_transients(device, graph);
}

bool RenderGraphResourceSet::has_allocatable_transients(const CompiledRenderGraph& graph) {
    for (const RenderGraphTextureRequirement& requirement : graph.resource_signature().textures) {
        if (requirement.lifetime == RenderGraphResourceLifetime::Transient &&
            requirement.usage_flags != 0) {
            return true;
        }
    }
    for (const RenderGraphBufferRequirement& requirement : graph.resource_signature().buffers) {
        if (requirement.lifetime == RenderGraphResourceLifetime::Transient &&
            requirement.usage_flags != 0) {
            return true;
        }
    }
    return false;
}

bool RenderGraphResourceSet::compatible(const CompiledRenderGraph& graph) const {
    return resource_signature_ == graph.resource_signature();
}

void RenderGraphResourceSet::reset(const CompiledRenderGraph& graph) {
    if (!compatible(graph)) {
        throw std::runtime_error("render graph resource set cannot reset to incompatible graph");
    }
    reset_compatible();
}

void RenderGraphResourceSet::reset_compatible() {
    textures_.assign(resource_signature_.textures.size(), std::nullopt);
    buffers_.assign(resource_signature_.buffers.size(), std::nullopt);
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
    if (buffer.byte_size < resource_signature_.buffers[index].byte_size) {
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
    if (!has_allocatable_transients(graph)) {
        return;
    }
    for (std::size_t index = 0; index < graph.textures().size(); ++index) {
        const RenderGraphTextureResource& texture = graph.textures()[index];
        const RenderGraphTextureRequirement& requirement = resource_signature_.textures[index];
        if (requirement.lifetime != RenderGraphResourceLifetime::Transient) {
            continue;
        }
        if (requirement.usage_flags == 0) {
            continue;
        }
        transient_textures_.emplace_back(
            device,
            cubey::vulkan::ImageConfig{
                .extent = requirement.extent,
                .format = requirement.format,
                .usage = requirement.usage_flags,
                .aspect = requirement.aspects,
                .image_type = requirement.extent.depth > 1U ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
                .view_type =
                    requirement.extent.depth > 1U ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D,
            });
        const cubey::vulkan::Image& image = transient_textures_.back();
        const RenderGraphResolvedTexture resolved{
            .image = image.handle(),
            .view = image.view(),
        };
        transient_texture_bindings_.emplace_back(texture.handle, resolved);
        bind_texture(texture.handle, resolved);
    }

    for (std::size_t index = 0; index < graph.buffers().size(); ++index) {
        const RenderGraphBufferResource& buffer = graph.buffers()[index];
        const RenderGraphBufferRequirement& requirement = resource_signature_.buffers[index];
        if (requirement.lifetime != RenderGraphResourceLifetime::Transient) {
            continue;
        }
        if (requirement.usage_flags == 0) {
            continue;
        }
        transient_buffers_.emplace_back(
            device, cubey::vulkan::BufferConfig{
                        .size = requirement.byte_size,
                        .usage = requirement.usage_flags,
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

void RenderGraphResourceSet::bind_transient_resources() {
    for (const auto& [handle, resolved] : transient_texture_bindings_) {
        bind_texture(handle, resolved);
    }
    for (const auto& [handle, resolved] : transient_buffer_bindings_) {
        bind_buffer(handle, resolved);
    }
}

} // namespace cubey::render
