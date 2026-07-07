#include <cubey/render/render_graph_builder.h>

#include "render_graph_private.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace cubey::render {
namespace {

void validate_label(const std::string& label, const char* message) {
    if (label.empty()) {
        throw std::runtime_error(message);
    }
}

void validate_texture_desc(const RenderGraphTextureDesc& desc) {
    validate_label(desc.label, "render graph texture label must be non-empty");
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        throw std::runtime_error("render graph texture extent must be nonzero");
    }
    if (desc.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("render graph texture format must be defined");
    }
    if (!detail::is_render_graph_color_aspect(desc.aspects) &&
        !detail::is_render_graph_depth_aspect(desc.aspects)) {
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

[[nodiscard]] bool is_transfer_texture_usage(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::TransferRead ||
           usage == RenderGraphTextureUsage::TransferWrite;
}

[[nodiscard]] bool is_transfer_buffer_usage(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::TransferRead ||
           usage == RenderGraphBufferUsage::TransferWrite;
}

[[nodiscard]] bool is_shader_texture_usage(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::SampledRead ||
           usage == RenderGraphTextureUsage::StorageRead ||
           usage == RenderGraphTextureUsage::StorageWrite ||
           usage == RenderGraphTextureUsage::StorageReadWrite;
}

[[nodiscard]] bool is_shader_buffer_usage(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::UniformRead ||
           usage == RenderGraphBufferUsage::StorageRead ||
           usage == RenderGraphBufferUsage::StorageWrite ||
           usage == RenderGraphBufferUsage::StorageReadWrite;
}

[[nodiscard]] constexpr VkPipelineStageFlags graphics_shader_stage_mask() noexcept {
    return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
           VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
           VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
}

[[nodiscard]] constexpr VkPipelineStageFlags compute_shader_stage_mask() noexcept {
    return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

void validate_shader_stage_mask_for_pass(const RenderGraphCompiledPass& pass,
                                         VkPipelineStageFlags stage_mask) {
    if (stage_mask == 0) {
        return;
    }
    if (pass.queue_domain == RenderGraphQueueDomain::Graphics) {
        if ((stage_mask & ~graphics_shader_stage_mask()) != 0) {
            throw std::runtime_error(
                "render graph graphics shader stage mask must use graphics shader stages");
        }
        return;
    }
    if (pass.queue_domain == RenderGraphQueueDomain::Compute) {
        if ((stage_mask & ~compute_shader_stage_mask()) != 0) {
            throw std::runtime_error(
                "render graph compute shader stage mask must use compute shader stage");
        }
    }
}

void validate_texture_usage_for_pass(const RenderGraphCompiledPass& pass,
                                     const RenderGraphTextureResource& resource,
                                     RenderGraphTextureUsage usage,
                                     VkPipelineStageFlags stage_mask) {
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
        !detail::is_render_graph_depth_aspect(resource.desc.aspects)) {
        throw std::runtime_error("render graph depth attachment requires a depth texture");
    }
    if ((usage == RenderGraphTextureUsage::ColorAttachment ||
         usage == RenderGraphTextureUsage::StorageRead ||
         usage == RenderGraphTextureUsage::StorageWrite ||
         usage == RenderGraphTextureUsage::StorageReadWrite) &&
        !detail::is_render_graph_color_aspect(resource.desc.aspects)) {
        throw std::runtime_error("render graph color/storage usage requires a color texture");
    }
    if (stage_mask != 0 && !is_shader_texture_usage(usage)) {
        throw std::runtime_error("render graph explicit texture stage mask requires shader usage");
    }
    if (is_shader_texture_usage(usage)) {
        validate_shader_stage_mask_for_pass(pass, stage_mask);
    }
}

void validate_buffer_usage_for_pass(const RenderGraphCompiledPass& pass,
                                    RenderGraphBufferUsage usage,
                                    VkPipelineStageFlags stage_mask) {
    if (pass.queue_domain == RenderGraphQueueDomain::Transfer && !is_transfer_buffer_usage(usage)) {
        throw std::runtime_error("render graph transfer pass can only use transfer buffer usages");
    }
    if ((usage == RenderGraphBufferUsage::VertexRead ||
         usage == RenderGraphBufferUsage::IndexRead) &&
        pass.queue_domain != RenderGraphQueueDomain::Graphics) {
        throw std::runtime_error("render graph vertex/index buffer usage requires a graphics pass");
    }
    if (stage_mask != 0 && !is_shader_buffer_usage(usage)) {
        throw std::runtime_error("render graph explicit buffer stage mask requires shader usage");
    }
    if (is_shader_buffer_usage(usage)) {
        validate_shader_stage_mask_for_pass(pass, stage_mask);
    }
}

} // namespace

RenderGraphPassBuilder::RenderGraphPassBuilder(RenderGraphBuilder& graph, std::uint32_t pass_index)
    : graph_(&graph), pass_index_(pass_index) {}

RenderGraphPassBuilder& RenderGraphPassBuilder::read_texture(RenderGraphTextureHandle handle,
                                                             VkPipelineStageFlags stage_mask) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::SampledRead,
                               stage_mask);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_storage_texture(RenderGraphTextureHandle handle,
                                             VkPipelineStageFlags stage_mask) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::StorageRead,
                               stage_mask);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::write_storage_texture(RenderGraphTextureHandle handle,
                                              VkPipelineStageFlags stage_mask) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::StorageWrite,
                               stage_mask);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_write_storage_texture(RenderGraphTextureHandle handle,
                                                   VkPipelineStageFlags stage_mask) {
    graph_->add_texture_access(pass_index_, handle, RenderGraphTextureUsage::StorageReadWrite,
                               stage_mask);
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
RenderGraphPassBuilder::read_uniform_buffer(RenderGraphBufferHandle handle,
                                            VkPipelineStageFlags stage_mask) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::UniformRead,
                              stage_mask);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_storage_buffer(RenderGraphBufferHandle handle,
                                            VkPipelineStageFlags stage_mask) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::StorageRead,
                              stage_mask);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::write_storage_buffer(RenderGraphBufferHandle handle,
                                             VkPipelineStageFlags stage_mask) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::StorageWrite,
                              stage_mask);
    return *this;
}

RenderGraphPassBuilder&
RenderGraphPassBuilder::read_write_storage_buffer(RenderGraphBufferHandle handle,
                                                  VkPipelineStageFlags stage_mask) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::StorageReadWrite,
                              stage_mask);
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::read_vertex_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::VertexRead);
    return *this;
}

RenderGraphPassBuilder& RenderGraphPassBuilder::read_index_buffer(RenderGraphBufferHandle handle) {
    graph_->add_buffer_access(pass_index_, handle, RenderGraphBufferUsage::IndexRead);
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

RenderGraphTextureHandle
RenderGraphBuilder::import_color_target(std::string label, ColorTargetView target,
                                        std::optional<RenderGraphTextureState> initial_state,
                                        std::optional<RenderGraphTextureState> final_state) {
    return import_texture(
        RenderGraphTextureDesc{
            .label = std::move(label),
            .extent = {target.extent.width, target.extent.height, 1},
            .format = target.format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        },
        target.image, target.view, initial_state, final_state);
}

RenderGraphTextureHandle
RenderGraphBuilder::import_depth_target(std::string label, DepthTargetView target,
                                        std::optional<RenderGraphTextureState> initial_state,
                                        std::optional<RenderGraphTextureState> final_state) {
    return import_texture(
        RenderGraphTextureDesc{
            .label = std::move(label),
            .extent = {target.extent.width, target.extent.height, 1},
            .format = target.format,
            .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        },
        target.image, target.view, initial_state, final_state);
}

RenderGraphTextureHandle
RenderGraphBuilder::import_texture(RenderGraphTextureDesc desc, VkImage image, VkImageView view,
                                   std::optional<RenderGraphTextureState> initial_state,
                                   std::optional<RenderGraphTextureState> final_state) {
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
        .initial_state = initial_state,
        .final_state = final_state,
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

RenderGraphBufferHandle
RenderGraphBuilder::import_buffer(RenderGraphBufferDesc desc, VkBuffer buffer,
                                  std::optional<RenderGraphBufferState> initial_state,
                                  std::optional<RenderGraphBufferState> final_state) {
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
        .initial_state = initial_state,
        .final_state = final_state,
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

void RenderGraphBuilder::add_texture_access(std::uint32_t pass_index,
                                            RenderGraphTextureHandle handle,
                                            RenderGraphTextureUsage usage,
                                            VkPipelineStageFlags stage_mask) {
    if (pass_index >= passes_.size()) {
        throw std::runtime_error("render graph pass handle is invalid");
    }
    const RenderGraphTextureResource& resource = texture_resource(handle);
    RenderGraphCompiledPass& pass = passes_[pass_index];
    validate_texture_usage_for_pass(pass, resource, usage, stage_mask);
    pass.texture_accesses.push_back(RenderGraphTextureAccess{
        .handle = handle,
        .usage = usage,
        .stage_mask = stage_mask,
    });
}

void RenderGraphBuilder::add_buffer_access(std::uint32_t pass_index, RenderGraphBufferHandle handle,
                                           RenderGraphBufferUsage usage,
                                           VkPipelineStageFlags stage_mask) {
    if (pass_index >= passes_.size()) {
        throw std::runtime_error("render graph pass handle is invalid");
    }
    const RenderGraphBufferResource& resource = buffer_resource(handle);
    static_cast<void>(resource);
    RenderGraphCompiledPass& pass = passes_[pass_index];
    validate_buffer_usage_for_pass(pass, usage, stage_mask);
    pass.buffer_accesses.push_back(RenderGraphBufferAccess{
        .handle = handle,
        .usage = usage,
        .stage_mask = stage_mask,
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
