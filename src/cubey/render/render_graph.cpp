#include <cubey/render/render_graph.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::render {
namespace {

[[nodiscard]] bool is_color_aspect(VkImageAspectFlags aspects) {
    return aspects == VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] bool is_depth_aspect(VkImageAspectFlags aspects) {
    return aspects == VK_IMAGE_ASPECT_DEPTH_BIT;
}

void validate_label(const std::string& label, const char* message) {
    if (label.empty()) {
        throw std::runtime_error(message);
    }
}

void validate_texture_desc(const RenderGraphTextureDesc& desc) {
    validate_label(desc.label, "render graph texture label must be non-empty");
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        throw std::runtime_error("render graph texture extent must be nonzero");
    }
    if (desc.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("render graph texture format must be defined");
    }
    if (!is_color_aspect(desc.aspects) && !is_depth_aspect(desc.aspects)) {
        throw std::runtime_error("render graph texture aspect must be color or depth");
    }
}

void validate_buffer_desc(const RenderGraphBufferDesc& desc) {
    validate_label(desc.label, "render graph buffer label must be non-empty");
    if (desc.byte_size == 0) {
        throw std::runtime_error("render graph buffer byte size must be nonzero");
    }
}

void validate_next_resource_index(std::size_t resource_count, const char* message) {
    if (resource_count >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool is_texture_read(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::SampledRead ||
           usage == RenderGraphTextureUsage::StorageRead ||
           usage == RenderGraphTextureUsage::TransferRead;
}

[[nodiscard]] bool is_texture_write(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::StorageWrite ||
           usage == RenderGraphTextureUsage::ColorAttachment ||
           usage == RenderGraphTextureUsage::DepthAttachment ||
           usage == RenderGraphTextureUsage::TransferWrite;
}

[[nodiscard]] bool is_buffer_read(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::UniformRead ||
           usage == RenderGraphBufferUsage::StorageRead ||
           usage == RenderGraphBufferUsage::TransferRead;
}

[[nodiscard]] bool is_buffer_write(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::StorageWrite ||
           usage == RenderGraphBufferUsage::TransferWrite;
}

[[nodiscard]] bool is_transfer_texture_usage(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::TransferRead ||
           usage == RenderGraphTextureUsage::TransferWrite;
}

[[nodiscard]] bool is_transfer_buffer_usage(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::TransferRead ||
           usage == RenderGraphBufferUsage::TransferWrite;
}

void validate_texture_usage_for_pass(const RenderGraphCompiledPass& pass,
                                     const RenderGraphTextureResource& resource,
                                     RenderGraphTextureUsage usage) {
    if (pass.queue_domain == RenderGraphQueueDomain::Transfer &&
        !is_transfer_texture_usage(usage)) {
        throw std::runtime_error("render graph transfer pass can only use transfer texture usages");
    }
    if ((usage == RenderGraphTextureUsage::ColorAttachment ||
         usage == RenderGraphTextureUsage::DepthAttachment) &&
        pass.queue_domain != RenderGraphQueueDomain::Graphics) {
        throw std::runtime_error("render graph attachment usage requires a graphics pass");
    }
    if (usage == RenderGraphTextureUsage::DepthAttachment &&
        !is_depth_aspect(resource.desc.aspects)) {
        throw std::runtime_error("render graph depth attachment requires a depth texture");
    }
    if ((usage == RenderGraphTextureUsage::ColorAttachment ||
         usage == RenderGraphTextureUsage::StorageRead ||
         usage == RenderGraphTextureUsage::StorageWrite) &&
        !is_color_aspect(resource.desc.aspects)) {
        throw std::runtime_error("render graph color/storage usage requires a color texture");
    }
}

void validate_buffer_usage_for_pass(const RenderGraphCompiledPass& pass,
                                    RenderGraphBufferUsage usage) {
    if (pass.queue_domain == RenderGraphQueueDomain::Transfer &&
        !is_transfer_buffer_usage(usage)) {
        throw std::runtime_error("render graph transfer pass can only use transfer buffer usages");
    }
}

template <typename AccessT, typename HandleT>
void validate_unique_accesses(const std::vector<AccessT>& accesses, HandleT AccessT::*handle_member,
                              const char* message) {
    for (auto current = accesses.begin(); current != accesses.end(); ++current) {
        const HandleT current_handle = (*current).*handle_member;
        const auto duplicate = std::find_if(
            accesses.begin(), current,
            [current_handle, handle_member](const AccessT& prior) {
                return (prior.*handle_member).index == current_handle.index;
            });
        if (duplicate != current) {
            throw std::runtime_error(message);
        }
    }
}

} // namespace

CompiledRenderGraph::CompiledRenderGraph(std::vector<RenderGraphTextureResource> textures,
                                         std::vector<RenderGraphBufferResource> buffers,
                                         std::vector<RenderGraphCompiledPass> passes)
    : textures_(std::move(textures)), buffers_(std::move(buffers)), passes_(std::move(passes)) {}

const RenderGraphTextureResource&
CompiledRenderGraph::texture(RenderGraphTextureHandle handle) const {
    if (!handle || handle.index > textures_.size()) {
        throw std::runtime_error("render graph texture handle is invalid");
    }
    return textures_[static_cast<std::size_t>(handle.index - 1U)];
}

const RenderGraphBufferResource&
CompiledRenderGraph::buffer(RenderGraphBufferHandle handle) const {
    if (!handle || handle.index > buffers_.size()) {
        throw std::runtime_error("render graph buffer handle is invalid");
    }
    return buffers_[static_cast<std::size_t>(handle.index - 1U)];
}

RenderGraphPassBuilder::RenderGraphPassBuilder(RenderGraphBuilder& graph,
                                               std::uint32_t pass_index)
    : graph_(&graph), pass_index_(pass_index) {}

RenderGraphPassBuilder& RenderGraphPassBuilder::read_texture(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::SampledRead);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_storage_texture(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::StorageRead);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::write_storage_texture(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::StorageWrite);
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::write_color(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::ColorAttachment);
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::write_depth(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::DepthAttachment);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::transfer_read_texture(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::TransferRead);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::transfer_write_texture(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::TransferWrite);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_uniform_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::UniformRead);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_storage_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::StorageRead);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::write_storage_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::StorageWrite);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::transfer_read_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::TransferRead);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::transfer_write_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::TransferWrite);
    return *this;
}

RenderGraphTextureHandle RenderGraphBuilder::import_color_target(std::string label,
                                                                 ColorTargetView target) {
    return import_texture(
        RenderGraphTextureDesc{
            .label = std::move(label),
            .extent = target.extent,
            .format = target.format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        },
        target.image, target.view);
}

RenderGraphTextureHandle RenderGraphBuilder::import_depth_target(std::string label,
                                                                 DepthTargetView target) {
    return import_texture(
        RenderGraphTextureDesc{
            .label = std::move(label),
            .extent = target.extent,
            .format = target.format,
            .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        },
        target.image, target.view);
}

RenderGraphTextureHandle RenderGraphBuilder::import_texture(RenderGraphTextureDesc desc,
                                                            VkImage image, VkImageView view) {
    validate_texture_desc(desc);
    if (image == VK_NULL_HANDLE || view == VK_NULL_HANDLE) {
        throw std::runtime_error("render graph imported texture requires valid image and view");
    }
    validate_next_resource_index(textures_.size(), "render graph texture resource store is full");

    const RenderGraphTextureHandle handle{
        .index = static_cast<std::uint32_t>(textures_.size() + 1U),
    };
    textures_.push_back(RenderGraphTextureResource{
        .handle = handle,
        .lifetime = RenderGraphResourceLifetime::Imported,
        .desc = std::move(desc),
        .imported_image = image,
        .imported_view = view,
    });
    return handle;
}

RenderGraphTextureHandle RenderGraphBuilder::create_texture(RenderGraphTextureDesc desc) {
    validate_texture_desc(desc);
    validate_next_resource_index(textures_.size(), "render graph texture resource store is full");

    const RenderGraphTextureHandle handle{
        .index = static_cast<std::uint32_t>(textures_.size() + 1U),
    };
    textures_.push_back(RenderGraphTextureResource{
        .handle = handle,
        .lifetime = RenderGraphResourceLifetime::Transient,
        .desc = std::move(desc),
    });
    return handle;
}

RenderGraphBufferHandle RenderGraphBuilder::import_buffer(RenderGraphBufferDesc desc,
                                                          VkBuffer buffer) {
    validate_buffer_desc(desc);
    if (buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("render graph imported buffer requires a valid buffer");
    }
    validate_next_resource_index(buffers_.size(), "render graph buffer resource store is full");

    const RenderGraphBufferHandle handle{
        .index = static_cast<std::uint32_t>(buffers_.size() + 1U),
    };
    buffers_.push_back(RenderGraphBufferResource{
        .handle = handle,
        .lifetime = RenderGraphResourceLifetime::Imported,
        .desc = std::move(desc),
        .imported_buffer = buffer,
    });
    return handle;
}

RenderGraphBufferHandle RenderGraphBuilder::create_buffer(RenderGraphBufferDesc desc) {
    validate_buffer_desc(desc);
    validate_next_resource_index(buffers_.size(), "render graph buffer resource store is full");

    const RenderGraphBufferHandle handle{
        .index = static_cast<std::uint32_t>(buffers_.size() + 1U),
    };
    buffers_.push_back(RenderGraphBufferResource{
        .handle = handle,
        .lifetime = RenderGraphResourceLifetime::Transient,
        .desc = std::move(desc),
    });
    return handle;
}

RenderGraphPassBuilder RenderGraphBuilder::add_pass(std::string label,
                                                    RenderGraphQueueDomain domain) {
    validate_label(label, "render graph pass label must be non-empty");
    const std::uint32_t pass_index = static_cast<std::uint32_t>(passes_.size());
    passes_.push_back(RenderGraphCompiledPass{
        .label = std::move(label),
        .queue_domain = domain,
    });
    return RenderGraphPassBuilder(*this, pass_index);
}

CompiledRenderGraph RenderGraphBuilder::compile() const {
    std::vector<bool> texture_available(textures_.size(), false);
    std::vector<bool> buffer_available(buffers_.size(), false);

    for (std::size_t index = 0; index < textures_.size(); ++index) {
        texture_available[index] =
            textures_[index].lifetime == RenderGraphResourceLifetime::Imported;
    }
    for (std::size_t index = 0; index < buffers_.size(); ++index) {
        buffer_available[index] =
            buffers_[index].lifetime == RenderGraphResourceLifetime::Imported;
    }

    for (const RenderGraphCompiledPass& pass : passes_) {
        validate_unique_accesses(pass.texture_accesses, &RenderGraphTextureAccess::handle,
                                 "render graph pass cannot access a texture more than once");
        validate_unique_accesses(pass.buffer_accesses, &RenderGraphBufferAccess::handle,
                                 "render graph pass cannot access a buffer more than once");

        std::vector<std::size_t> texture_writes;
        std::vector<std::size_t> buffer_writes;

        for (const RenderGraphTextureAccess& access : pass.texture_accesses) {
            const RenderGraphTextureResource& resource = texture_resource(access.handle);
            const std::size_t index = static_cast<std::size_t>(access.handle.index - 1U);
            if (is_texture_read(access.usage) && !texture_available[index]) {
                throw std::runtime_error("render graph transient texture is read before write");
            }
            if (is_texture_write(access.usage)) {
                texture_writes.push_back(index);
            }
            static_cast<void>(resource);
        }

        for (const RenderGraphBufferAccess& access : pass.buffer_accesses) {
            const RenderGraphBufferResource& resource = buffer_resource(access.handle);
            const std::size_t index = static_cast<std::size_t>(access.handle.index - 1U);
            if (is_buffer_read(access.usage) && !buffer_available[index]) {
                throw std::runtime_error("render graph transient buffer is read before write");
            }
            if (is_buffer_write(access.usage)) {
                buffer_writes.push_back(index);
            }
            static_cast<void>(resource);
        }

        for (std::size_t index : texture_writes) {
            texture_available[index] = true;
        }
        for (std::size_t index : buffer_writes) {
            buffer_available[index] = true;
        }
    }

    return CompiledRenderGraph(textures_, buffers_, passes_);
}

void RenderGraphBuilder::add_texture_access(std::uint32_t pass_index,
                                            RenderGraphTextureHandle handle,
                                            RenderGraphTextureUsage usage) {
    if (pass_index >= passes_.size()) {
        throw std::runtime_error("render graph pass handle is invalid");
    }
    const RenderGraphTextureResource& resource = texture_resource(handle);
    RenderGraphCompiledPass& pass = passes_[pass_index];
    validate_texture_usage_for_pass(pass, resource, usage);
    pass.texture_accesses.push_back(RenderGraphTextureAccess{
        .handle = handle,
        .usage = usage,
    });
}

void RenderGraphBuilder::add_buffer_access(std::uint32_t pass_index, RenderGraphBufferHandle handle,
                                           RenderGraphBufferUsage usage) {
    if (pass_index >= passes_.size()) {
        throw std::runtime_error("render graph pass handle is invalid");
    }
    const RenderGraphBufferResource& resource = buffer_resource(handle);
    static_cast<void>(resource);
    RenderGraphCompiledPass& pass = passes_[pass_index];
    validate_buffer_usage_for_pass(pass, usage);
    pass.buffer_accesses.push_back(RenderGraphBufferAccess{
        .handle = handle,
        .usage = usage,
    });
}

const RenderGraphTextureResource&
RenderGraphBuilder::texture_resource(RenderGraphTextureHandle handle) const {
    if (!handle || handle.index > textures_.size()) {
        throw std::runtime_error("render graph texture handle is invalid");
    }
    return textures_[static_cast<std::size_t>(handle.index - 1U)];
}

const RenderGraphBufferResource&
RenderGraphBuilder::buffer_resource(RenderGraphBufferHandle handle) const {
    if (!handle || handle.index > buffers_.size()) {
        throw std::runtime_error("render graph buffer handle is invalid");
    }
    return buffers_[static_cast<std::size_t>(handle.index - 1U)];
}

} // namespace cubey::render
