#include <cubey/render/cloud_planar_reflection.h>

#include <cubey/render/pass.h>
#include <cubey/render/target.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace cubey::render {
namespace {

struct CloudPlanarFilterPushConstants {
    float radius = 0.0F;
};

[[nodiscard]] math::Vec3 reflect_vector(math::Vec3 value, math::Vec3 normal) {
    return value - 2.0F * glm::dot(value, normal) * normal;
}

[[nodiscard]] Texture2DConfig contribution_texture_config(VkExtent2D extent, VkFormat format,
                                                           bool create_sampler) {
    return {
        .extent = extent,
        .mip_levels = 1U,
        .format = format,
        .usage = Texture2DUsage::StorageSampled,
        .create_sampler = create_sampler,
        .sampler = {
            .min_filter = VK_FILTER_LINEAR,
            .mag_filter = VK_FILTER_LINEAR,
            .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        },
    };
}

[[nodiscard]] Texture2DConfig filtered_texture_config(VkExtent2D extent, VkFormat format,
                                                       std::uint32_t mip_levels) {
    return {
        .extent = extent,
        .mip_levels = mip_levels,
        .format = format,
        .usage = Texture2DUsage::ColorAttachmentSampled,
        .create_sampler = true,
        .sampler = {
            .min_filter = VK_FILTER_LINEAR,
            .mag_filter = VK_FILTER_LINEAR,
            .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .max_lod = static_cast<float>(mip_levels - 1U),
        },
    };
}

} // namespace

MaterialPassInfo cloud_planar_reflection_filter_pass_info() {
    return MaterialPassInfo{
        .label = "cloud.planar.filter",
        .descriptor_sets = {
            MaterialDescriptorSetLayout{
                .set = 0,
                .bindings = {
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 0,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                },
            },
        },
        .push_constants = {
            VkPushConstantRange{
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = sizeof(CloudPlanarFilterPushConstants),
            },
        },
    };
}

void validate_cloud_planar_reflection_config(const CloudPlanarReflectionConfig& config) {
    if (config.target_extent.width == 0U || config.target_extent.height == 0U ||
        config.frame_slot_count == 0U || config.mip_levels == 0U || config.view_steps == 0U) {
        throw std::runtime_error("cloud planar reflection dimensions must be positive");
    }
    if (!std::isfinite(config.resolution_scale) || config.resolution_scale <= 0.0F ||
        config.resolution_scale > 1.0F) {
        throw std::runtime_error("cloud planar reflection resolution scale is invalid");
    }
    if (!std::isfinite(config.guard_band) || config.guard_band < 0.0F ||
        config.guard_band > 0.5F) {
        throw std::runtime_error("cloud planar reflection guard band is invalid");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("cloud planar reflection requires a color format");
    }
    const VkExtent2D extent = cloud_planar_reflection_extent(config);
    const std::uint32_t max_dimension = std::max(extent.width, extent.height);
    const std::uint32_t max_mips =
        static_cast<std::uint32_t>(std::floor(std::log2(max_dimension))) + 1U;
    if (config.mip_levels > max_mips) {
        throw std::runtime_error("cloud planar reflection mip count exceeds its extent");
    }
}

VkExtent2D cloud_planar_reflection_extent(const CloudPlanarReflectionConfig& config) {
    return {
        .width = std::max(1U, static_cast<std::uint32_t>(std::lround(
                                static_cast<float>(config.target_extent.width) *
                                config.resolution_scale))),
        .height = std::max(1U, static_cast<std::uint32_t>(std::lround(
                                 static_cast<float>(config.target_extent.height) *
                                 config.resolution_scale))),
    };
}

CloudLayerFrameInfo cloud_planar_reflected_frame(const CloudLayerFrameInfo& frame,
                                                 math::Vec3 plane_point,
                                                 math::Vec3 plane_normal,
                                                 VkExtent2D target_extent,
                                                 float guard_band) {
    if (target_extent.width == 0U || target_extent.height == 0U ||
        !std::isfinite(guard_band) || guard_band < 0.0F) {
        throw std::runtime_error("cloud planar reflected frame inputs are invalid");
    }
    const float normal_length = glm::length(plane_normal);
    if (!std::isfinite(normal_length) || normal_length <= 0.0001F) {
        throw std::runtime_error("cloud planar reflection plane normal is invalid");
    }
    const math::Vec3 normal = plane_normal / normal_length;
    CloudLayerFrameInfo reflected = frame;
    reflected.camera_position =
        frame.camera_position -
        2.0F * glm::dot(frame.camera_position - plane_point, normal) * normal;
    reflected.camera_right = glm::normalize(reflect_vector(frame.camera_right, normal));
    reflected.camera_up = glm::normalize(reflect_vector(frame.camera_up, normal));
    reflected.camera_forward = glm::normalize(reflect_vector(frame.camera_forward, normal));
    reflected.tan_half_fovy = frame.tan_half_fovy * (1.0F + guard_band);
    reflected.target_extent = target_extent;
    reflected.temporal_frame_index = 0U;
    reflected.camera_mode = 0.0F;
    reflected.external_background = false;
    reflected.scene_depth_occlusion_enabled = false;
    return reflected;
}

void CloudPlanarReflectionRuntime::create_resources(
    const cubey::vulkan::Device& device, const CloudPlanarReflectionConfig& config,
    const CloudLayerGeneratedResources& generated) {
    validate_cloud_planar_reflection_config(config);
    destroy();
    config_ = config;
    const VkExtent2D extent = cloud_planar_reflection_extent(config_);
    buffers_.resize(config_.frame_slot_count);
    for (Buffer& slot : buffers_) {
        slot.contribution.emplace(device, contribution_texture_config(extent, config_.format, true));
        slot.metadata.emplace(device, contribution_texture_config(extent, config_.format, false));
        slot.filtered.emplace(device,
                              filtered_texture_config(extent, config_.format, config_.mip_levels));
        slot.filtered_mips.reserve(config_.mip_levels);
        for (std::uint32_t mip = 0U; mip < config_.mip_levels; ++mip) {
            slot.filtered_mips.push_back(create_texture_2d_mip_view(device, *slot.filtered, mip));
        }
        slot.filtered_initialized.assign(config_.mip_levels, false);
    }

    march_material_ =
        std::make_unique<FrameUniformMaterialInstance<CloudLayerFrameUniforms>>(
            device, FrameUniformMaterialInstanceConfig{
                        .material_pass = cloud_layer_march_pass_info(),
                        .descriptor_set = 0,
                        .frame_slot_count = config_.frame_slot_count,
                        .uniform_binding = kCloudLayerUniformBinding,
                        .sampled_images = {
                            {kCloudLayerBaseNoiseBinding, generated.base_noise.sampler().handle(),
                             generated.base_noise.view()},
                            {kCloudLayerDetailNoiseBinding,
                             generated.detail_noise.sampler().handle(),
                             generated.detail_noise.view()},
                            {kCloudLayerWeatherBinding, generated.weather.sampler().handle(),
                             generated.weather.view()},
                            {kCloudLayerBlueNoiseBinding, generated.blue_noise.sampler().handle(),
                             generated.blue_noise.view()},
                        },
                    });
    for (std::uint32_t slot_index = 0U; slot_index < config_.frame_slot_count; ++slot_index) {
        const FrameSlot frame_slot{.index = slot_index, .count = config_.frame_slot_count};
        Buffer& slot = buffers_[slot_index];
        MaterialDescriptorWriter(march_material_->set(frame_slot))
            .storage_image(kCloudLayerOutputBinding, slot.contribution->view())
            .storage_image(kCloudLayerMetadataBinding, slot.metadata->view())
            .update(device);
    }

    filter_materials_.reserve(config_.mip_levels);
    for (std::uint32_t mip = 0U; mip < config_.mip_levels; ++mip) {
        auto material = std::make_unique<MaterialInstance>(
            device, MaterialInstanceConfig{
                        .material_pass = cloud_planar_reflection_filter_pass_info(),
                        .descriptor_set = 0,
                        .set_count = config_.frame_slot_count,
                    });
        for (std::uint32_t slot_index = 0U; slot_index < config_.frame_slot_count; ++slot_index) {
            const FrameSlot frame_slot{.index = slot_index, .count = config_.frame_slot_count};
            Buffer& slot = buffers_[slot_index];
            const VkSampler source_sampler = mip == 0U ? slot.contribution->sampler().handle()
                                                        : slot.filtered->sampler().handle();
            const VkImageView source_view =
                mip == 0U ? slot.contribution->view() : slot.filtered_mips[mip - 1U].handle();
            MaterialDescriptorWriter(material->set(frame_slot))
                .combined_image_sampler(0U, source_sampler, source_view)
                .update(device);
        }
        filter_materials_.push_back(std::move(material));
    }
}

void CloudPlanarReflectionRuntime::create_pipelines(
    const cubey::vulkan::Device& device,
    const CloudPlanarReflectionPipelineConfig& config) {
    if (!resources_created()) {
        throw std::runtime_error("cloud planar reflection resources are not initialized");
    }
    if (config.cloud_march.path.empty() || config.filter_vertex.path.empty() ||
        config.filter_fragment.path.empty()) {
        throw std::runtime_error("cloud planar reflection requires shader files");
    }
    const std::array<VkDescriptorSetLayout, 1> march_layouts{march_material_->layout()};
    march_pipeline_.emplace(device, ComputePipelineResourceConfig{
                                        .shader_stage = config.cloud_march,
                                        .descriptor_set_layouts = march_layouts,
                                    });
    const std::array<VkDescriptorSetLayout, 1> filter_layouts{filter_material(0U).layout()};
    const std::array<ShaderStageFile, 2> shaders{config.filter_vertex, config.filter_fragment};
    filter_pipeline_.emplace(
        device, graphics_pipeline_file_resource_config(
                    {.extent = cloud_planar_reflection_extent(config_),
                     .color_format = config_.format},
                    {.shader_stage_files = shaders,
                     .descriptor_set_layouts = filter_layouts,
                     .material_pass = cloud_planar_reflection_filter_pass_info()}));
}

void CloudPlanarReflectionRuntime::destroy() {
    filter_pipeline_.reset();
    march_pipeline_.reset();
    filter_materials_.clear();
    march_material_.reset();
    buffers_.clear();
}

void CloudPlanarReflectionRuntime::record(
    const cubey::vulkan::CommandRecorder& recorder,
    const CloudPlanarReflectionRequest& request) {
    if (!resources_created() || !pipelines_created()) {
        throw std::runtime_error("cloud planar reflection is not initialized");
    }
    Buffer& slot = buffer(request.frame_slot);
    const VkExtent2D extent = cloud_planar_reflection_extent(config_);
    const CloudLayerFrameInfo reflected = cloud_planar_reflected_frame(
        request.frame, request.plane_point, request.plane_normal, extent, config_.guard_band);
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    slot.view_rays = {
        .right_aspect = {reflected.camera_right.x, reflected.camera_right.y,
                         reflected.camera_right.z, aspect},
        .up_tan_half_fovy = {reflected.camera_up.x, reflected.camera_up.y,
                             reflected.camera_up.z, reflected.tan_half_fovy},
        .forward = {reflected.camera_forward.x, reflected.camera_forward.y,
                    reflected.camera_forward.z, 0.0F},
    };

    CloudLayerConfig cloud = request.cloud;
    cloud.quality = CloudLayerQuality::Full;
    cloud.view_steps_override = static_cast<std::int32_t>(config_.view_steps);
    cloud.view_samples = 1;
    cloud.temporal_enabled = false;
    cloud.sampling_mode = CloudLayerSamplingMode::Bayer;
    cloud.distance_mode = CloudLayerDistanceMode::Local;
    cloud.density_model = CloudLayerDensityModel::SurfaceVolume;
    cloud.debug_view = CloudLayerDebugView::Final;
    march_material_->upload(request.frame_slot, cloud_layer_frame_uniforms(cloud, reflected));

    transition_mip(recorder, slot.contribution->handle(), 0U,
                   slot.contribution_initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL,
                   slot.contribution_initialized ? VK_ACCESS_SHADER_READ_BIT : 0U,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   slot.contribution_initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                 : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    transition_mip(recorder, slot.metadata->handle(), 0U,
                   slot.metadata_initialized ? VK_IMAGE_LAYOUT_GENERAL
                                             : VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_GENERAL,
                   slot.metadata_initialized ? VK_ACCESS_SHADER_WRITE_BIT : 0U,
                   VK_ACCESS_SHADER_WRITE_BIT,
                   slot.metadata_initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    record_compute_pipeline_dispatch(
        recorder,
        compute_pipeline_dispatch_info(*march_pipeline_, march_material_->set(request.frame_slot),
                                       ceil_dispatch_groups(extent.width, extent.height,
                                                            kCloudLayerComputeGroupSize)));
    transition_mip(recorder, slot.contribution->handle(), 0U, VK_IMAGE_LAYOUT_GENERAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    slot.contribution_initialized = true;
    slot.metadata_initialized = true;

    for (std::uint32_t mip = 0U; mip < config_.mip_levels; ++mip) {
        const VkExtent2D mip_extent = texture_2d_mip_extent(extent, mip);
        transition_mip(
            recorder, slot.filtered->handle(), mip,
            slot.filtered_initialized[mip] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                           : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            slot.filtered_initialized[mip] ? VK_ACCESS_SHADER_READ_BIT : 0U,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            slot.filtered_initialized[mip] ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                           : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        MaterialInstance& material = filter_material(mip);
        record_render_target_pass(
            recorder,
            render_target_view(color_target_view(mip_extent, config_.format,
                                                 slot.filtered->handle(),
                                                 slot.filtered_mips[mip].handle())),
            RenderClearValues{.color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F)},
            [this, &material, frame_slot = request.frame_slot, mip](
                const cubey::vulkan::CommandRecorder& pass_recorder) {
                record_fullscreen_pipeline_draw(
                    pass_recorder,
                    {.pipeline = &*filter_pipeline_,
                     .descriptor_set = material.set(frame_slot),
                     .descriptor_set_index = 0},
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    CloudPlanarFilterPushConstants{.radius = mip == 0U ? 0.0F : 1.0F});
            });
        transition_mip(recorder, slot.filtered->handle(), mip,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        slot.filtered_initialized[mip] = true;
    }
    slot.valid = true;
}

CloudPlanarReflectionSnapshot
CloudPlanarReflectionRuntime::snapshot(FrameSlot frame_slot) const {
    const Buffer& slot = buffer(frame_slot);
    if (!slot.valid || !slot.filtered.has_value()) {
        return {};
    }
    return {
        .texture = &slot.filtered.value(),
        .view_rays = slot.view_rays,
        .max_lod = static_cast<float>(config_.mip_levels - 1U),
        .valid = true,
    };
}

bool CloudPlanarReflectionRuntime::resources_created() const noexcept {
    return buffers_.size() == config_.frame_slot_count && !buffers_.empty() &&
           buffers_.front().contribution.has_value() && buffers_.front().filtered.has_value() &&
           march_material_ != nullptr && filter_materials_.size() == config_.mip_levels;
}

bool CloudPlanarReflectionRuntime::pipelines_created() const noexcept {
    return march_pipeline_.has_value() && filter_pipeline_.has_value();
}

void CloudPlanarReflectionRuntime::transition_mip(
    const cubey::vulkan::CommandRecorder& recorder, VkImage image,
    std::uint32_t mip_level, VkImageLayout old_layout, VkImageLayout new_layout,
    VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
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

CloudPlanarReflectionRuntime::Buffer&
CloudPlanarReflectionRuntime::buffer(FrameSlot frame_slot) {
    validate_frame_slot(frame_slot);
    if (frame_slot.count != config_.frame_slot_count || frame_slot.index >= buffers_.size()) {
        throw std::runtime_error("cloud planar reflection frame slot mismatch");
    }
    return buffers_[frame_slot.index];
}

const CloudPlanarReflectionRuntime::Buffer&
CloudPlanarReflectionRuntime::buffer(FrameSlot frame_slot) const {
    validate_frame_slot(frame_slot);
    if (frame_slot.count != config_.frame_slot_count || frame_slot.index >= buffers_.size()) {
        throw std::runtime_error("cloud planar reflection frame slot mismatch");
    }
    return buffers_[frame_slot.index];
}

MaterialInstance&
CloudPlanarReflectionRuntime::filter_material(std::uint32_t mip_level) const {
    if (mip_level >= filter_materials_.size() || filter_materials_[mip_level] == nullptr) {
        throw std::runtime_error("cloud planar reflection filter material is not initialized");
    }
    return *filter_materials_[mip_level];
}

} // namespace cubey::render
