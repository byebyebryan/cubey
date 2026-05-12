#include <cubey/render/render_graph.h>

#include <algorithm>
#include <limits>
#include <span>
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
           usage == RenderGraphTextureUsage::StorageReadWrite ||
           usage == RenderGraphTextureUsage::TransferRead;
}

[[nodiscard]] bool is_texture_write(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::StorageWrite ||
           usage == RenderGraphTextureUsage::StorageReadWrite ||
           usage == RenderGraphTextureUsage::ColorAttachment ||
           usage == RenderGraphTextureUsage::DepthAttachment ||
           usage == RenderGraphTextureUsage::TransferWrite;
}

[[nodiscard]] bool is_buffer_read(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::UniformRead ||
           usage == RenderGraphBufferUsage::StorageRead ||
           usage == RenderGraphBufferUsage::StorageReadWrite ||
           usage == RenderGraphBufferUsage::TransferRead;
}

[[nodiscard]] bool is_buffer_write(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::StorageWrite ||
           usage == RenderGraphBufferUsage::StorageReadWrite ||
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
         usage == RenderGraphTextureUsage::StorageWrite ||
         usage == RenderGraphTextureUsage::StorageReadWrite) &&
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

[[nodiscard]] VkPipelineStageFlags shader_stage_for_pass(RenderGraphQueueDomain domain) {
    if (domain == RenderGraphQueueDomain::Compute) {
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
}

struct TextureUsageState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags access_mask = 0;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
};

[[nodiscard]] TextureUsageState texture_usage_state(const RenderGraphCompiledPass& pass,
                                                    const RenderGraphTextureResource& resource,
                                                    RenderGraphTextureUsage usage) {
    switch (usage) {
    case RenderGraphTextureUsage::SampledRead:
        return {
            .layout = is_depth_aspect(resource.desc.aspects)
                          ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::StorageRead:
        return {
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::StorageWrite:
        return {
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::StorageReadWrite:
        return {
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::ColorAttachment:
        return {
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    case RenderGraphTextureUsage::DepthAttachment:
        return {
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        };
    case RenderGraphTextureUsage::TransferRead:
        return {
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    case RenderGraphTextureUsage::TransferWrite:
        return {
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    }
    throw std::runtime_error("render graph texture usage is invalid");
}

struct BufferUsageState {
    VkAccessFlags access_mask = 0;
    VkPipelineStageFlags stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
};

[[nodiscard]] BufferUsageState buffer_usage_state(const RenderGraphCompiledPass& pass,
                                                  RenderGraphBufferUsage usage) {
    switch (usage) {
    case RenderGraphBufferUsage::UniformRead:
        return {
            .access_mask = VK_ACCESS_UNIFORM_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::StorageRead:
        return {
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::StorageWrite:
        return {
            .access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::StorageReadWrite:
        return {
            .access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::TransferRead:
        return {
            .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    case RenderGraphBufferUsage::TransferWrite:
        return {
            .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    }
    throw std::runtime_error("render graph buffer usage is invalid");
}

struct LastTextureAccess {
    bool valid = false;
    std::size_t pass_index = 0;
    RenderGraphTextureUsage usage = RenderGraphTextureUsage::SampledRead;
};

struct LastBufferAccess {
    bool valid = false;
    std::size_t pass_index = 0;
    RenderGraphBufferUsage usage = RenderGraphBufferUsage::UniformRead;
};

[[nodiscard]] bool needs_texture_barrier(RenderGraphTextureUsage previous,
                                         RenderGraphTextureUsage next) {
    return is_texture_write(previous) || is_texture_write(next);
}

[[nodiscard]] bool needs_buffer_barrier(RenderGraphBufferUsage previous,
                                        RenderGraphBufferUsage next) {
    return is_buffer_write(previous) || is_buffer_write(next);
}

[[nodiscard]] RenderGraphTextureBarrier
make_texture_barrier(RenderGraphTextureHandle handle, const RenderGraphTextureResource& resource,
                     const RenderGraphCompiledPass& source_pass,
                     const RenderGraphCompiledPass& destination_pass,
                     const LastTextureAccess& previous, RenderGraphTextureUsage next) {
    const TextureUsageState source = texture_usage_state(source_pass, resource, previous.usage);
    const TextureUsageState destination = texture_usage_state(destination_pass, resource, next);
    return RenderGraphTextureBarrier{
        .handle = handle,
        .source_pass_index = previous.pass_index,
        .source_usage = previous.usage,
        .destination_usage = next,
        .transition =
            cubey::vulkan::ImageLayoutTransition{
                .image = resource.imported_image,
                .aspect_mask = resource.desc.aspects,
                .old_layout = source.layout,
                .new_layout = destination.layout,
                .src_access_mask = source.access_mask,
                .dst_access_mask = destination.access_mask,
                .src_stage_mask = source.stage_mask,
                .dst_stage_mask = destination.stage_mask,
            },
    };
}

[[nodiscard]] RenderGraphBufferBarrier
make_buffer_barrier(RenderGraphBufferHandle handle, const RenderGraphBufferResource& resource,
                    const RenderGraphCompiledPass& source_pass,
                    const RenderGraphCompiledPass& destination_pass,
                    const LastBufferAccess& previous, RenderGraphBufferUsage next) {
    const BufferUsageState source = buffer_usage_state(source_pass, previous.usage);
    const BufferUsageState destination = buffer_usage_state(destination_pass, next);
    auto barrier = VkBufferMemoryBarrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = source.access_mask;
    barrier.dstAccessMask = destination.access_mask;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = resource.imported_buffer;
    barrier.offset = 0;
    barrier.size = resource.desc.byte_size;
    return RenderGraphBufferBarrier{
        .handle = handle,
        .source_pass_index = previous.pass_index,
        .source_usage = previous.usage,
        .destination_usage = next,
        .src_stage_mask = source.stage_mask,
        .dst_stage_mask = destination.stage_mask,
        .barrier = barrier,
    };
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

RenderGraphExecutionContext::RenderGraphExecutionContext(const CompiledRenderGraph& graph,
                                                         std::size_t pass_index)
    : graph_(&graph), pass_index_(pass_index) {}

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

void CompiledRenderGraph::execute() const {
    for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        const RenderGraphCompiledPass& pass = passes_[pass_index];
        if (!pass.execute) {
            throw std::runtime_error("render graph pass has no execute callback");
        }
        pass.execute(RenderGraphExecutionContext(*this, pass_index));
    }
}

void record_render_graph_barriers(const cubey::vulkan::CommandRecorder& recorder,
                                  const RenderGraphExecutionContext& context) {
    const RenderGraphCompiledPass& pass = context.pass();
    for (const RenderGraphTextureBarrier& barrier : pass.texture_barriers) {
        if (barrier.transition.image == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "render graph texture barrier requires an allocated texture");
        }
        const VkImageMemoryBarrier image_barrier =
            cubey::vulkan::image_memory_barrier(barrier.transition);
        recorder.pipeline_barrier(
            barrier.transition.src_stage_mask, barrier.transition.dst_stage_mask, 0, {}, {},
            std::span<const VkImageMemoryBarrier>(&image_barrier, 1));
    }
    for (const RenderGraphBufferBarrier& barrier : pass.buffer_barriers) {
        if (barrier.barrier.buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("render graph buffer barrier requires an allocated buffer");
        }
        recorder.pipeline_barrier(barrier.src_stage_mask, barrier.dst_stage_mask, 0, {},
                                  std::span<const VkBufferMemoryBarrier>(&barrier.barrier, 1),
                                  {});
    }
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

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_write_storage_texture(RenderGraphTextureHandle handle) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::StorageReadWrite);
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
RenderGraphPassBuilder::read_write_storage_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::StorageReadWrite);
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

RenderGraphPassBuilder& RenderGraphPassBuilder::material_pass(MaterialPassInfo info) {
    graph_->set_material_pass(pass_index_, std::move(info));
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::execute(RenderGraphExecuteCallback callback) {
    graph_->set_execute_callback(pass_index_, std::move(callback));
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
    std::vector<RenderGraphCompiledPass> compiled_passes = passes_;
    std::vector<bool> texture_available(textures_.size(), false);
    std::vector<bool> buffer_available(buffers_.size(), false);
    std::vector<LastTextureAccess> last_texture_accesses(textures_.size());
    std::vector<LastBufferAccess> last_buffer_accesses(buffers_.size());

    for (std::size_t index = 0; index < textures_.size(); ++index) {
        texture_available[index] =
            textures_[index].lifetime == RenderGraphResourceLifetime::Imported;
    }
    for (std::size_t index = 0; index < buffers_.size(); ++index) {
        buffer_available[index] =
            buffers_[index].lifetime == RenderGraphResourceLifetime::Imported;
    }

    for (std::size_t pass_index = 0; pass_index < compiled_passes.size(); ++pass_index) {
        RenderGraphCompiledPass& pass = compiled_passes[pass_index];
        validate_unique_accesses(pass.texture_accesses, &RenderGraphTextureAccess::handle,
                                 "render graph pass cannot access a texture more than once");
        validate_unique_accesses(pass.buffer_accesses, &RenderGraphBufferAccess::handle,
                                 "render graph pass cannot access a buffer more than once");

        std::vector<std::size_t> texture_writes;
        std::vector<std::size_t> buffer_writes;

        for (const RenderGraphTextureAccess& access : pass.texture_accesses) {
            const RenderGraphTextureResource& resource = texture_resource(access.handle);
            const std::size_t index = static_cast<std::size_t>(access.handle.index - 1U);
            if (is_texture_read(access.usage) && !is_texture_write(access.usage) &&
                !texture_available[index]) {
                throw std::runtime_error("render graph transient texture is read before write");
            }
            const LastTextureAccess& previous = last_texture_accesses[index];
            if (previous.valid && needs_texture_barrier(previous.usage, access.usage)) {
                pass.texture_barriers.push_back(make_texture_barrier(
                    access.handle, resource, compiled_passes[previous.pass_index], pass, previous,
                    access.usage));
            }
            if (is_texture_write(access.usage)) {
                texture_writes.push_back(index);
            }
            last_texture_accesses[index] = LastTextureAccess{
                .valid = true,
                .pass_index = pass_index,
                .usage = access.usage,
            };
            static_cast<void>(resource);
        }

        for (const RenderGraphBufferAccess& access : pass.buffer_accesses) {
            const RenderGraphBufferResource& resource = buffer_resource(access.handle);
            const std::size_t index = static_cast<std::size_t>(access.handle.index - 1U);
            if (is_buffer_read(access.usage) && !is_buffer_write(access.usage) &&
                !buffer_available[index]) {
                throw std::runtime_error("render graph transient buffer is read before write");
            }
            const LastBufferAccess& previous = last_buffer_accesses[index];
            if (previous.valid && needs_buffer_barrier(previous.usage, access.usage)) {
                pass.buffer_barriers.push_back(make_buffer_barrier(
                    access.handle, resource, compiled_passes[previous.pass_index], pass, previous,
                    access.usage));
            }
            if (is_buffer_write(access.usage)) {
                buffer_writes.push_back(index);
            }
            last_buffer_accesses[index] = LastBufferAccess{
                .valid = true,
                .pass_index = pass_index,
                .usage = access.usage,
            };
            static_cast<void>(resource);
        }

        for (std::size_t index : texture_writes) {
            texture_available[index] = true;
        }
        for (std::size_t index : buffer_writes) {
            buffer_available[index] = true;
        }
    }

    return CompiledRenderGraph(textures_, buffers_, compiled_passes);
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

void RenderGraphBuilder::set_material_pass(std::uint32_t pass_index, MaterialPassInfo info) {
    if (pass_index >= passes_.size()) {
        throw std::runtime_error("render graph pass handle is invalid");
    }
    validate_material_pass_info(info);
    passes_[pass_index].material_pass = std::move(info);
}

void RenderGraphBuilder::set_execute_callback(std::uint32_t pass_index,
                                              RenderGraphExecuteCallback callback) {
    if (pass_index >= passes_.size()) {
        throw std::runtime_error("render graph pass handle is invalid");
    }
    passes_[pass_index].execute = std::move(callback);
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
