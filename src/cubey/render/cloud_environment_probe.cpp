#include <cubey/render/cloud_environment_probe.h>

#include <cubey/render/pass.h>
#include <cubey/render/target.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace cubey::render {
namespace {

constexpr std::uint32_t kProbeBufferCount = 2U;
constexpr std::uint32_t kCubeFaceCount = 6U;

[[nodiscard]] std::size_t face_mip_index(std::uint32_t mip_level,
                                         std::uint32_t face_index) {
    return static_cast<std::size_t>(mip_level) * kCubeFaceCount +
           static_cast<std::size_t>(face_index);
}

[[nodiscard]] AtmosphereReflectionPrefilterUniforms
prefilter_uniforms(const ViewRayBasis3D& view_rays, float roughness,
                   std::uint32_t mip_level) {
    return {
        .right_roughness = {view_rays.right_aspect.x, view_rays.right_aspect.y,
                            view_rays.right_aspect.z, roughness},
        .up_sample_count = {view_rays.up_tan_half_fovy.x,
                            view_rays.up_tan_half_fovy.y,
                            view_rays.up_tan_half_fovy.z,
                            roughness <= 0.0001F ? 1.0F : 96.0F},
        .forward_mip_level = {view_rays.forward.x, view_rays.forward.y,
                              view_rays.forward.z, static_cast<float>(mip_level)},
    };
}

[[nodiscard]] CloudLayerFrameInfo cube_face_frame(const CloudLayerFrameInfo& frame,
                                                  const ViewRayBasis3D& view_rays,
                                                  std::uint32_t extent) {
    CloudLayerFrameInfo face = frame;
    face.camera_right = math::Vec3{view_rays.right_aspect};
    face.camera_up = math::Vec3{view_rays.up_tan_half_fovy};
    face.camera_forward = math::Vec3{view_rays.forward};
    face.tan_half_fovy = 1.0F;
    face.target_extent = {extent, extent};
    face.camera_mode = 0.0F;
    face.external_background = false;
    face.scene_depth_mode = CloudLayerSceneDepthMode::Disabled;
    return face;
}

[[nodiscard]] TextureCubeConfig contribution_texture_config(
    const CloudEnvironmentProbeConfig& config, bool create_sampler) {
    return {
        .extent = config.extent,
        .mip_levels = 1,
        .format = config.format,
        .usage = TextureCubeUsage::StorageSampled,
        .create_sampler = create_sampler,
        .sampler = {.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
    };
}

[[nodiscard]] TextureCubeConfig prefiltered_texture_config(
    const CloudEnvironmentProbeConfig& config) {
    return {
        .extent = config.extent,
        .mip_levels = config.mip_levels,
        .format = config.format,
        .usage = TextureCubeUsage::ColorAttachmentSampled,
        .create_sampler = true,
        .sampler = {
            .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .max_lod = static_cast<float>(config.mip_levels - 1U),
        },
    };
}

} // namespace

MaterialPassInfo cloud_environment_prefilter_pass_info() {
    return MaterialPassInfo{
        .label = "cloud.environment.prefilter",
        .descriptor_sets = {
            MaterialDescriptorSetLayout{
                .set = 0,
                .bindings = {
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 0,
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 1,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                    cubey::vulkan::DescriptorSetBindingConfig{
                        .binding = 2,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    },
                },
            },
        },
    };
}

void validate_cloud_environment_probe_config(const CloudEnvironmentProbeConfig& config) {
    if (config.extent == 0U || config.mip_levels == 0U || config.view_steps == 0U ||
        config.frame_slot_count == 0U) {
        throw std::runtime_error("cloud environment probe dimensions must be positive");
    }
    const std::uint32_t max_mips =
        static_cast<std::uint32_t>(std::floor(std::log2(config.extent))) + 1U;
    if (config.mip_levels > max_mips) {
        throw std::runtime_error("cloud environment probe mip count exceeds its extent");
    }
    if (!std::isfinite(config.update_hz) || config.update_hz <= 0.0F) {
        throw std::runtime_error("cloud environment probe update rate must be positive");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("cloud environment probe requires a color format");
    }
}

void CloudEnvironmentProbeTimeline::configure(float update_hz) {
    if (!std::isfinite(update_hz) || update_hz <= 0.0F) {
        throw std::runtime_error("cloud environment timeline update rate must be positive");
    }
    update_interval_seconds_ = 1.0F / update_hz;
    reset();
}

void CloudEnvironmentProbeTimeline::reset() {
    age_seconds_ = 0.0F;
    blend_ = 1.0F;
    generation_ = 0;
    valid_ = false;
    capture_pending_ = true;
}

void CloudEnvironmentProbeTimeline::advance(double delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) {
        throw std::runtime_error("cloud environment timeline delta must be finite and non-negative");
    }
    if (!valid_) {
        return;
    }
    const float delta = static_cast<float>(delta_seconds);
    age_seconds_ += delta;
    blend_ = std::min(1.0F, blend_ + delta / update_interval_seconds_);
    if (blend_ >= 1.0F && age_seconds_ >= update_interval_seconds_) {
        capture_pending_ = true;
    }
}

void CloudEnvironmentProbeTimeline::capture_recorded() {
    const bool had_valid_capture = valid_;
    valid_ = true;
    capture_pending_ = false;
    age_seconds_ = 0.0F;
    blend_ = had_valid_capture ? 0.0F : 1.0F;
    ++generation_;
}

void CloudEnvironmentProbe::create_resources(
    const cubey::vulkan::Device& device, const CloudEnvironmentProbeConfig& config,
    const CloudLayerGeneratedResources& generated, const TextureCube& clear_sky) {
    validate_cloud_environment_probe_config(config);
    if (!clear_sky.has_sampler()) {
        throw std::runtime_error("cloud environment probe clear sky requires a sampler");
    }
    destroy();
    config_ = config;
    timeline_.configure(config.update_hz);
    for (ProbeBuffer& buffer : buffers_) {
        create_buffer_resources(device, buffer, generated, clear_sky);
    }
}

void CloudEnvironmentProbe::create_buffer_resources(
    const cubey::vulkan::Device& device, ProbeBuffer& buffer,
    const CloudLayerGeneratedResources& generated, const TextureCube& clear_sky) {
    buffer.contribution.emplace(device, contribution_texture_config(config_, true));
    buffer.metadata.emplace(device, contribution_texture_config(config_, false));
    buffer.prefiltered.emplace(device, prefiltered_texture_config(config_));
    buffer.contribution_faces.reserve(kCubeFaceCount);
    buffer.metadata_faces.reserve(kCubeFaceCount);
    for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
        buffer.contribution_faces.push_back(
            create_texture_cube_face_view(device, *buffer.contribution, 0, face));
        buffer.metadata_faces.push_back(
            create_texture_cube_face_view(device, *buffer.metadata, 0, face));
    }
    buffer.prefiltered_faces.reserve(
        static_cast<std::size_t>(config_.mip_levels) * kCubeFaceCount);
    for (std::uint32_t mip = 0; mip < config_.mip_levels; ++mip) {
        for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
            buffer.prefiltered_faces.push_back(
                create_texture_cube_face_view(device, *buffer.prefiltered, mip, face));
        }
    }
    buffer.prefiltered_initialized.assign(
        static_cast<std::size_t>(config_.mip_levels) * kCubeFaceCount, false);

    const MaterialPassInfo march_pass = cloud_layer_march_pass_info();
    for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
        buffer.march_materials[face] =
            std::make_unique<FrameUniformMaterialInstance<CloudLayerFrameUniforms>>(
                device,
                FrameUniformMaterialInstanceConfig{
                    .material_pass = march_pass,
                    .descriptor_set = 0,
                    .frame_slot_count = config_.frame_slot_count,
                    .uniform_binding = kCloudLayerUniformBinding,
                    .sampled_images = {
                        {kCloudLayerBaseNoiseBinding,
                         generated.base_noise.sampler().handle(),
                         generated.base_noise.view()},
                        {kCloudLayerDetailNoiseBinding,
                         generated.detail_noise.sampler().handle(),
                         generated.detail_noise.view()},
                        {kCloudLayerWeatherBinding, generated.weather.sampler().handle(),
                         generated.weather.view()},
                        {kCloudLayerBlueNoiseBinding,
                         generated.blue_noise.sampler().handle(),
                         generated.blue_noise.view()},
                    },
                });
        for (std::uint32_t slot = 0; slot < config_.frame_slot_count; ++slot) {
            const FrameSlot frame_slot{.index = slot, .count = config_.frame_slot_count};
            MaterialDescriptorWriter(buffer.march_materials[face]->set(frame_slot))
                .storage_image(kCloudLayerOutputBinding,
                               buffer.contribution_faces[face].handle())
                .storage_image(kCloudLayerMetadataBinding,
                               buffer.metadata_faces[face].handle())
                .update(device);
        }
    }

    const MaterialPassInfo prefilter_pass = cloud_environment_prefilter_pass_info();
    buffer.prefilter_materials.reserve(
        static_cast<std::size_t>(config_.mip_levels) * kCubeFaceCount);
    for (std::uint32_t mip = 0; mip < config_.mip_levels; ++mip) {
        for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
            buffer.prefilter_materials.push_back(std::make_unique<
                FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>>(
                device,
                FrameUniformMaterialInstanceConfig{
                    .material_pass = prefilter_pass,
                    .descriptor_set = 0,
                    .frame_slot_count = config_.frame_slot_count,
                    .uniform_binding = 0,
                    .sampled_images = {
                        {1, clear_sky.sampler().handle(), clear_sky.view()},
                        {2, buffer.contribution->sampler().handle(),
                         buffer.contribution->view()},
                    },
                }));
        }
    }
}

void CloudEnvironmentProbe::create_pipelines(
    const cubey::vulkan::Device& device,
    const CloudEnvironmentProbePipelineConfig& config) {
    if (!resources_created()) {
        throw std::runtime_error("cloud environment probe resources are not initialized");
    }
    if (config.cloud_march.path.empty() || config.prefilter_vertex.path.empty() ||
        config.prefilter_fragment.path.empty()) {
        throw std::runtime_error("cloud environment probe requires shader files");
    }
    const std::array<VkDescriptorSetLayout, 1> march_layouts{
        march_material(buffers_[0], 0).layout()};
    march_pipeline_.emplace(device, ComputePipelineResourceConfig{
                                        .shader_stage = config.cloud_march,
                                        .descriptor_set_layouts = march_layouts,
                                    });
    const std::array<VkDescriptorSetLayout, 1> prefilter_layouts{
        prefilter_material(buffers_[0], 0, 0).layout()};
    const std::array<ShaderStageFile, 2> prefilter_shaders{
        config.prefilter_vertex,
        config.prefilter_fragment,
    };
    prefilter_pipeline_.emplace(
        device,
        graphics_pipeline_file_resource_config(
            {.extent = {config_.extent, config_.extent}, .color_format = config_.format},
            {.shader_stage_files = prefilter_shaders,
             .descriptor_set_layouts = prefilter_layouts,
             .material_pass = cloud_environment_prefilter_pass_info()}));
}

void CloudEnvironmentProbe::destroy() {
    prefilter_pipeline_.reset();
    march_pipeline_.reset();
    for (ProbeBuffer& buffer : buffers_) {
        buffer.prefilter_materials.clear();
        for (auto& material : buffer.march_materials) {
            material.reset();
        }
        buffer.prefiltered_faces.clear();
        buffer.metadata_faces.clear();
        buffer.contribution_faces.clear();
        buffer.prefiltered.reset();
        buffer.metadata.reset();
        buffer.contribution.reset();
        buffer.contribution_initialized.fill(false);
        buffer.metadata_initialized.fill(false);
        buffer.prefiltered_initialized.clear();
    }
    timeline_.reset();
    previous_buffer_index_ = 0;
    current_buffer_index_ = 0;
}

void CloudEnvironmentProbe::advance(double delta_seconds) {
    timeline_.advance(delta_seconds);
}

void CloudEnvironmentProbe::invalidate() {
    timeline_.reset();
    previous_buffer_index_ = 0;
    current_buffer_index_ = 0;
}

bool CloudEnvironmentProbe::record_pending_update(
    const cubey::vulkan::CommandRecorder& recorder,
    const CloudEnvironmentProbeUpdateInfo& info) {
    if (!timeline_.capture_pending()) {
        return false;
    }
    if (!resources_created() || !pipelines_created()) {
        throw std::runtime_error("cloud environment probe is not initialized");
    }
    std::uint32_t write_index = 0;
    if (timeline_.valid()) {
        previous_buffer_index_ = current_buffer_index_;
        write_index = (current_buffer_index_ + 1U) % kProbeBufferCount;
        current_buffer_index_ = write_index;
    } else {
        previous_buffer_index_ = 0;
        current_buffer_index_ = 0;
    }
    record_capture(recorder, info.frame_slot, info.cloud, info.frame, write_index);
    timeline_.capture_recorded();
    return true;
}

void CloudEnvironmentProbe::record_capture(
    const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot,
    const CloudLayerConfig& cloud, const CloudLayerFrameInfo& frame,
    std::uint32_t buffer_index) {
    ProbeBuffer& buffer = buffers_.at(buffer_index);
    record_cloud_faces(recorder, frame_slot, cloud, frame, buffer);
    record_prefilter_faces(recorder, frame_slot, buffer);
}

void CloudEnvironmentProbe::record_cloud_faces(
    const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot,
    const CloudLayerConfig& cloud, const CloudLayerFrameInfo& frame,
    ProbeBuffer& buffer) {
    const std::array<ViewRayBasis3D, kCubeFaceCount> face_view_rays =
        atmosphere_reflection_probe_cube_face_view_rays();
    CloudLayerConfig probe_cloud = cloud;
    probe_cloud.quality = CloudLayerQuality::Full;
    probe_cloud.view_steps_override = static_cast<std::int32_t>(config_.view_steps);
    probe_cloud.view_samples = 1;
    probe_cloud.temporal_enabled = false;
    probe_cloud.sampling_mode = CloudLayerSamplingMode::Bayer;
    probe_cloud.distance_mode = CloudLayerDistanceMode::Local;
    probe_cloud.density_model = CloudLayerDensityModel::SurfaceVolume;
    probe_cloud.debug_view = CloudLayerDebugView::Final;

    for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
        const VkImageLayout contribution_old =
            buffer.contribution_initialized[face]
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED;
        transition_face(recorder, buffer.contribution->handle(), 0, face,
                        contribution_old, VK_IMAGE_LAYOUT_GENERAL,
                        buffer.contribution_initialized[face] ? VK_ACCESS_SHADER_READ_BIT : 0,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        buffer.contribution_initialized[face]
                            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        transition_face(recorder, buffer.metadata->handle(), 0, face,
                        buffer.metadata_initialized[face] ? VK_IMAGE_LAYOUT_GENERAL
                                                          : VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL,
                        buffer.metadata_initialized[face] ? VK_ACCESS_SHADER_WRITE_BIT : 0,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        buffer.metadata_initialized[face]
                            ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        FrameUniformMaterialInstance<CloudLayerFrameUniforms>& material =
            march_material(buffer, face);
        material.upload(frame_slot,
                        cloud_layer_frame_uniforms(
                            probe_cloud,
                            cube_face_frame(frame, face_view_rays[face], config_.extent)));
        record_compute_pipeline_dispatch(
            recorder,
            compute_pipeline_dispatch_info(
                *march_pipeline_, material.set(frame_slot),
                ceil_dispatch_groups(config_.extent, config_.extent,
                                     kCloudLayerComputeGroupSize)));
        buffer.contribution_initialized[face] = true;
        buffer.metadata_initialized[face] = true;
    }

    for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
        transition_face(recorder, buffer.contribution->handle(), 0, face,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
}

void CloudEnvironmentProbe::record_prefilter_faces(
    const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot,
    ProbeBuffer& buffer) {
    const std::array<ViewRayBasis3D, kCubeFaceCount> face_view_rays =
        atmosphere_reflection_probe_cube_face_view_rays();
    for (std::uint32_t mip = 0; mip < config_.mip_levels; ++mip) {
        const float roughness =
            config_.mip_levels == 1U
                ? 0.0F
                : static_cast<float>(mip) /
                      static_cast<float>(config_.mip_levels - 1U);
        for (std::uint32_t face = 0; face < kCubeFaceCount; ++face) {
            const std::size_t index = face_mip_index(mip, face);
            FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>& material =
                prefilter_material(buffer, mip, face);
            material.upload(frame_slot,
                            prefilter_uniforms(face_view_rays[face], roughness, mip));
            transition_face(
                recorder, buffer.prefiltered->handle(), mip, face,
                buffer.prefiltered_initialized[index]
                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                buffer.prefiltered_initialized[index] ? VK_ACCESS_SHADER_READ_BIT : 0,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                buffer.prefiltered_initialized[index]
                    ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                    : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            const std::uint32_t mip_extent =
                texture_cube_mip_extent(config_.extent, mip);
            record_render_target_pass(
                recorder,
                render_target_view(color_target_view(
                    {mip_extent, mip_extent}, config_.format,
                    buffer.prefiltered->handle(),
                    buffer.prefiltered_faces[index].handle())),
                RenderClearValues{.color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F)},
                [this, frame_slot, &material](
                    const cubey::vulkan::CommandRecorder& pass_recorder) {
                    record_fullscreen_pipeline_draw(
                        pass_recorder,
                        {.pipeline = &*prefilter_pipeline_,
                         .descriptor_set = material.set(frame_slot),
                         .descriptor_set_index = 0});
                });
            transition_face(recorder, buffer.prefiltered->handle(), mip, face,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            buffer.prefiltered_initialized[index] = true;
        }
    }
}

bool CloudEnvironmentProbe::resources_created() const noexcept {
    return buffers_[0].contribution.has_value() &&
           buffers_[0].prefiltered.has_value() &&
           buffers_[1].contribution.has_value() &&
           buffers_[1].prefiltered.has_value();
}

bool CloudEnvironmentProbe::pipelines_created() const noexcept {
    return march_pipeline_.has_value() && prefilter_pipeline_.has_value();
}

CloudEnvironmentProbeSnapshot CloudEnvironmentProbe::snapshot() const {
    if (!timeline_.valid() || !resources_created()) {
        return {};
    }
    return {
        .previous = &*buffers_[previous_buffer_index_].prefiltered,
        .current = &*buffers_[current_buffer_index_].prefiltered,
        .blend = timeline_.blend(),
        .generation = timeline_.generation(),
        .valid = true,
    };
}

FrameUniformMaterialInstance<CloudLayerFrameUniforms>&
CloudEnvironmentProbe::march_material(ProbeBuffer& buffer,
                                      std::uint32_t face_index) const {
    if (face_index >= kCubeFaceCount || buffer.march_materials[face_index] == nullptr) {
        throw std::runtime_error("cloud environment march material is not initialized");
    }
    return *buffer.march_materials[face_index];
}

FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>&
CloudEnvironmentProbe::prefilter_material(ProbeBuffer& buffer,
                                          std::uint32_t mip_level,
                                          std::uint32_t face_index) const {
    const std::size_t index = face_mip_index(mip_level, face_index);
    if (mip_level >= config_.mip_levels || face_index >= kCubeFaceCount ||
        index >= buffer.prefilter_materials.size() ||
        buffer.prefilter_materials[index] == nullptr) {
        throw std::runtime_error("cloud environment prefilter material is not initialized");
    }
    return *buffer.prefilter_materials[index];
}

void CloudEnvironmentProbe::transition_face(
    const cubey::vulkan::CommandRecorder& recorder, VkImage image,
    std::uint32_t mip_level, std::uint32_t face_index, VkImageLayout old_layout,
    VkImageLayout new_layout, VkAccessFlags src_access, VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) const {
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
        .level_count = 1,
        .base_array_layer = face_index,
        .layer_count = 1,
    });
}

} // namespace cubey::render
