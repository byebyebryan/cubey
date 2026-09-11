#include <cubey/render/hdr_color_pyramid.h>

#include <cubey/render/pass.h>
#include <cubey/render/target.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace cubey::render {
namespace {

struct HdrColorPyramidFilterPushConstants {
    float copy_source = 0.0F;
    float bright_sample_resistance = 0.0F;
};

[[nodiscard]] Texture2DConfig hdr_color_pyramid_texture_config(VkExtent2D extent, VkFormat format,
                                                               std::uint32_t mip_levels) {
    return {
        .extent = extent,
        .mip_levels = mip_levels,
        .format = format,
        .usage = Texture2DUsage::ColorAttachmentSampled,
        .create_sampler = true,
        .sampler =
            {
                .min_filter = VK_FILTER_LINEAR,
                .mag_filter = VK_FILTER_LINEAR,
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .max_lod = static_cast<float>(mip_levels - 1U),
            },
    };
}

} // namespace

MaterialPassInfo hdr_color_pyramid_filter_pass_info() {
    return {
        .label = "hdr.color_pyramid.filter",
        .descriptor_sets =
            {
                MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0U,
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .push_constants =
            {
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .offset = 0U,
                    .size = sizeof(HdrColorPyramidFilterPushConstants),
                },
            },
    };
}

void validate_hdr_color_pyramid_config(const HdrColorPyramidConfig& config) {
    if (config.extent.width == 0U || config.extent.height == 0U || config.frame_slot_count == 0U ||
        config.minimum_mip_extent == 0U) {
        throw std::runtime_error("HDR color pyramid dimensions must be positive");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("HDR color pyramid requires a color format");
    }
}

std::uint32_t hdr_color_pyramid_mip_levels(const HdrColorPyramidConfig& config) {
    validate_hdr_color_pyramid_config(config);
    std::uint32_t levels = 1U;
    std::uint32_t maximum = std::max(config.extent.width, config.extent.height);
    while (maximum > config.minimum_mip_extent) {
        maximum = std::max(1U, maximum >> 1U);
        ++levels;
    }
    return levels;
}

void HdrColorPyramid::create_resources(const cubey::vulkan::Device& device,
                                       const HdrColorPyramidConfig& config) {
    validate_hdr_color_pyramid_config(config);
    destroy();
    config_ = config;
    const std::uint32_t mip_levels = hdr_color_pyramid_mip_levels(config_);
    buffers_.resize(config_.frame_slot_count);
    for (Buffer& slot : buffers_) {
        slot.texture.emplace(
            device, hdr_color_pyramid_texture_config(config_.extent, config_.format, mip_levels));
        slot.mip_views.reserve(mip_levels);
        for (std::uint32_t mip = 0U; mip < mip_levels; ++mip) {
            slot.mip_views.push_back(create_texture_2d_mip_view(device, *slot.texture, mip));
        }
    }

    filter_materials_.reserve(mip_levels);
    for (std::uint32_t mip = 0U; mip < mip_levels; ++mip) {
        auto material = std::make_unique<MaterialInstance>(
            device, MaterialInstanceConfig{
                        .material_pass = hdr_color_pyramid_filter_pass_info(),
                        .descriptor_set = 0U,
                        .set_count = config_.frame_slot_count,
                    });
        if (mip > 0U) {
            for (std::uint32_t slot_index = 0U; slot_index < config_.frame_slot_count;
                 ++slot_index) {
                const FrameSlot frame_slot{.index = slot_index, .count = config_.frame_slot_count};
                Buffer& slot = buffers_[slot_index];
                MaterialDescriptorWriter(material->set(frame_slot))
                    .combined_image_sampler(0U, slot.texture->sampler().handle(),
                                            slot.mip_views[mip - 1U].handle())
                    .update(device);
            }
        }
        filter_materials_.push_back(std::move(material));
    }
}

void HdrColorPyramid::create_pipeline(const cubey::vulkan::Device& device,
                                      const HdrColorPyramidPipelineConfig& config) {
    if (!resources_created()) {
        throw std::runtime_error("HDR color pyramid resources are not initialized");
    }
    if (config.vertex.path.empty() || config.fragment.path.empty()) {
        throw std::runtime_error("HDR color pyramid requires shader files");
    }
    const std::array<VkDescriptorSetLayout, 1> layouts{filter_material(0U).layout()};
    const std::array<ShaderStageFile, 2> shaders{config.vertex, config.fragment};
    filter_pipeline_.emplace(device, graphics_pipeline_file_resource_config(
                                         {
                                             .extent = config_.extent,
                                             .color_format = config_.format,
                                         },
                                         {
                                             .shader_stage_files = shaders,
                                             .descriptor_set_layouts = layouts,
                                             .material_pass = hdr_color_pyramid_filter_pass_info(),
                                         }));
}

void HdrColorPyramid::destroy() {
    filter_pipeline_.reset();
    filter_materials_.clear();
    buffers_.clear();
}

void HdrColorPyramid::update_source_descriptor(const cubey::vulkan::Device& device,
                                               FrameSlot frame_slot, VkSampler sampler,
                                               VkImageView source_view,
                                               VkImageLayout source_layout) {
    if (!resources_created() || sampler == VK_NULL_HANDLE || source_view == VK_NULL_HANDLE) {
        throw std::runtime_error("HDR color pyramid source descriptor is invalid");
    }
    static_cast<void>(buffer(frame_slot));
    MaterialDescriptorWriter(filter_material(0U).set(frame_slot))
        .combined_image_sampler(0U, sampler, source_view, source_layout)
        .update(device);
}

void HdrColorPyramid::record(const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot) {
    if (!resources_created() || !pipeline_created()) {
        throw std::runtime_error("HDR color pyramid is not initialized");
    }
    Buffer& slot = buffer(frame_slot);
    const std::uint32_t mip_levels = hdr_color_pyramid_mip_levels(config_);
    for (std::uint32_t mip = 0U; mip < mip_levels; ++mip) {
        transition_mip(
            recorder, slot.texture->handle(), mip,
            slot.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            slot.initialized ? VK_ACCESS_SHADER_READ_BIT : 0U, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            slot.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        const VkExtent2D mip_extent = texture_2d_mip_extent(config_.extent, mip);
        MaterialInstance& material = filter_material(mip);
        record_render_target_pass(
            recorder,
            render_target_view(color_target_view(mip_extent, config_.format, slot.texture->handle(),
                                                 slot.mip_views[mip].handle())),
            RenderClearValues{.color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F)},
            [this, &material, frame_slot,
             mip](const cubey::vulkan::CommandRecorder& pass_recorder) {
                record_fullscreen_pipeline_draw(
                    pass_recorder,
                    {
                        .pipeline = &filter_pipeline_.value(),
                        .descriptor_set = material.set(frame_slot),
                        .descriptor_set_index = 0U,
                    },
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    HdrColorPyramidFilterPushConstants{
                        .copy_source = mip == 0U ? 1.0F : 0.0F,
                        .bright_sample_resistance = mip == 1U ? 1.0F : 0.0F,
                    });
            });
        transition_mip(
            recorder, slot.texture->handle(), mip, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    slot.initialized = true;
    slot.valid = true;
}

HdrColorPyramidSnapshot HdrColorPyramid::snapshot(FrameSlot frame_slot) const {
    const Buffer& slot = buffer(frame_slot);
    if (!slot.texture.has_value()) {
        return {};
    }
    return {
        .texture = &slot.texture.value(),
        .max_lod = static_cast<float>(hdr_color_pyramid_mip_levels(config_) - 1U),
        .valid = slot.valid,
    };
}

bool HdrColorPyramid::resources_created() const noexcept {
    return buffers_.size() == config_.frame_slot_count && !buffers_.empty() &&
           buffers_.front().texture.has_value() && !filter_materials_.empty();
}

bool HdrColorPyramid::pipeline_created() const noexcept {
    return filter_pipeline_.has_value();
}

void HdrColorPyramid::transition_mip(const cubey::vulkan::CommandRecorder& recorder, VkImage image,
                                     std::uint32_t mip_level, VkImageLayout old_layout,
                                     VkImageLayout new_layout, VkAccessFlags src_access,
                                     VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                                     VkPipelineStageFlags dst_stage) const {
    recorder.transition_image_layout(cubey::vulkan::ImageLayoutTransition{
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = old_layout,
        .new_layout = new_layout,
        .src_access_mask = src_access,
        .dst_access_mask = dst_access,
        .src_stage_mask = src_stage,
        .dst_stage_mask = dst_stage,
        .base_mip_level = mip_level,
        .level_count = 1U,
        .base_array_layer = 0U,
        .layer_count = 1U,
    });
}

HdrColorPyramid::Buffer& HdrColorPyramid::buffer(FrameSlot frame_slot) {
    validate_frame_slot(frame_slot);
    if (frame_slot.count != config_.frame_slot_count || frame_slot.index >= buffers_.size()) {
        throw std::runtime_error("HDR color pyramid frame slot mismatch");
    }
    return buffers_[frame_slot.index];
}

const HdrColorPyramid::Buffer& HdrColorPyramid::buffer(FrameSlot frame_slot) const {
    validate_frame_slot(frame_slot);
    if (frame_slot.count != config_.frame_slot_count || frame_slot.index >= buffers_.size()) {
        throw std::runtime_error("HDR color pyramid frame slot mismatch");
    }
    return buffers_[frame_slot.index];
}

MaterialInstance& HdrColorPyramid::filter_material(std::uint32_t mip_level) const {
    if (mip_level >= filter_materials_.size() || filter_materials_[mip_level] == nullptr) {
        throw std::runtime_error("HDR color pyramid filter material is not initialized");
    }
    return *filter_materials_[mip_level];
}

} // namespace cubey::render
