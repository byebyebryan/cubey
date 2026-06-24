#include <cubey/render/cloud_layer.h>

#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace cubey::render {
namespace {

[[nodiscard]] MaterialDescriptorSetLayout cloud_layer_march_set_layout() {
    return {
        .set = 0,
        .bindings =
            {
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerUniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerOutputBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerBaseNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerDetailNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerWeatherBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerMetadataBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
            },
    };
}

[[nodiscard]] MaterialDescriptorSetLayout cloud_layer_composite_set_layout(
    bool external_background) {
    MaterialDescriptorSetLayout layout{
        .set = 0,
        .bindings =
            {
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerUniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerCompositeCloudBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerCompositeMetadataBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            },
    };
    if (external_background) {
        layout.bindings.push_back(vulkan::DescriptorSetBindingConfig{
            .binding = kCloudLayerCompositeBackgroundBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        });
    }
    return layout;
}

[[nodiscard]] MaterialDescriptorSetLayout cloud_layer_temporal_set_layout() {
    constexpr VkShaderStageFlags compute_stage = VK_SHADER_STAGE_COMPUTE_BIT;
    return {
        .set = 0,
        .bindings =
            {
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerTemporalCurrentCloudBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = compute_stage,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerTemporalCurrentMetadataBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = compute_stage,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerTemporalHistoryCloudBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = compute_stage,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerTemporalHistoryMetadataBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = compute_stage,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerTemporalUniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = compute_stage,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerTemporalOutputBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = compute_stage,
                },
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerTemporalOutputMetadataBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = compute_stage,
                },
            },
    };
}

[[nodiscard]] float sampling_mode_value(CloudLayerSamplingMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float background_mode_value(CloudLayerBackgroundMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float distance_mode_value(CloudLayerDistanceMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float orbit_representation_value(CloudLayerOrbitRepresentation mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float cloud_style_value(CloudLayerCloudStyle style) {
    return static_cast<float>(static_cast<std::uint32_t>(style));
}

void validate_compute_shader(const ShaderStageFile& shader, const char* label) {
    if (shader.path.empty()) {
        throw std::runtime_error(std::string(label) + " shader path is empty");
    }
    if (shader.stage != VK_SHADER_STAGE_COMPUTE_BIT) {
        throw std::runtime_error(std::string(label) + " shader must be a compute shader");
    }
}

template <typename PushConstants>
void generate_cloud_storage_texture(const cubey::vulkan::Device& device,
                                    cubey::vulkan::GpuRuntime& gpu, const char* label,
                                    const ShaderStageFile& shader, VkImage image,
                                    VkImageView view, PushConstants push_constants,
                                    ComputeDispatchGroups groups) {
    validate_compute_shader(shader, label);
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_image(descriptors.set(), 0, view);
    writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> layouts{descriptors.layout()};
    const ComputePipelineResource pipeline(
        device, ComputePipelineResourceConfig{
                    .shader_stage = shader,
                    .descriptor_set_layouts = layouts,
                    .push_constants = {{
                        VkPushConstantRange{
                            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                            .offset = 0,
                            .size = sizeof(PushConstants),
                        },
                    }},
                });

    static_cast<void>(gpu.submit_and_wait(cubey::vulkan::GpuWorkRequest{
        .label = label,
        .work =
            [image, &pipeline, descriptor_set = descriptors.set(), push_constants,
             groups](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(image));
                record_compute_pipeline_dispatch(
                    recorder, compute_pipeline_dispatch_info(pipeline, descriptor_set, groups),
                    VK_SHADER_STAGE_COMPUTE_BIT, push_constants);
                recorder.transition_image_layout(
                    cubey::vulkan::finish_storage_image_write_for_sampling_transition(image));
                commands.submit_and_wait();
            },
    }));
}

void generate_cloud_storage_volume_texture(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu, const char* label,
                                           const ShaderStageFile& shader,
                                           const Texture3D& texture,
                                           ComputeDispatchGroups groups) {
    validate_compute_shader(shader, label);
    const cubey::vulkan::ImageView storage_view(
        device, cubey::vulkan::ImageViewConfig{
                    .image = texture.handle(),
                    .format = texture.format(),
                    .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                    .view_type = VK_IMAGE_VIEW_TYPE_3D,
                    .base_mip_level = 0,
                    .level_count = 1,
                });

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_image(descriptors.set(), 0, storage_view.handle());
    writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> layouts{descriptors.layout()};
    const ComputePipelineResource pipeline(device, ComputePipelineResourceConfig{
                                                       .shader_stage = shader,
                                                       .descriptor_set_layouts = layouts,
                                                   });

    static_cast<void>(gpu.submit_and_wait(cubey::vulkan::GpuWorkRequest{
        .label = label,
        .work =
            [&texture, &pipeline, descriptor_set = descriptors.set(),
             groups](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(texture.handle()));
                record_compute_pipeline_dispatch(
                    recorder, compute_pipeline_dispatch_info(pipeline, descriptor_set, groups));
                if (texture.mip_levels() > 1U) {
                    record_generate_texture_3d_mips(commands.command_buffer(), texture,
                                                    VK_IMAGE_LAYOUT_GENERAL);
                } else {
                    recorder.transition_image_layout(
                        cubey::vulkan::finish_storage_image_write_for_sampling_transition(
                            texture.handle()));
                }
                commands.submit_and_wait();
            },
    }));
}

} // namespace

vulkan::SamplerConfig cloud_layer_repeat_sampler_config(std::uint32_t mip_levels) {
    return {
        .min_filter = VK_FILTER_LINEAR,
        .mag_filter = VK_FILTER_LINEAR,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .min_lod = 0.0F,
        .max_lod = static_cast<float>(std::max(1U, mip_levels) - 1U),
    };
}

std::uint32_t cloud_layer_generated_volume_mip_count(std::uint32_t size) {
    if (size == 0U) {
        throw std::runtime_error("cloud layer generated volume size must be nonzero");
    }
    std::uint32_t levels = 1U;
    while (size > 1U) {
        size = std::max(size / 2U, 1U);
        ++levels;
    }
    return levels;
}

Texture3DConfig cloud_layer_volume_texture_config(std::uint32_t size) {
    const std::uint32_t mip_levels = cloud_layer_generated_volume_mip_count(size);
    return {
        .extent = {size, size, size},
        .mip_levels = mip_levels,
        .format = kCloudLayerNoiseFormat,
        .create_sampler = true,
        .sampler = cloud_layer_repeat_sampler_config(mip_levels),
    };
}

Texture2DConfig cloud_layer_weather_texture_config() {
    return {
        .extent = {kCloudLayerWeatherTextureSize, kCloudLayerWeatherTextureSize},
        .format = kCloudLayerNoiseFormat,
        .usage = Texture2DUsage::StorageSampled,
        .create_sampler = true,
        .sampler = cloud_layer_repeat_sampler_config(),
    };
}

MaterialPassInfo cloud_layer_march_pass_info() {
    return {
        .label = "cloud_march",
        .descriptor_sets = {cloud_layer_march_set_layout()},
    };
}

MaterialPassInfo cloud_layer_composite_pass_info(bool external_background) {
    return {
        .label = external_background ? "cloud_composite_background" : "cloud_composite",
        .descriptor_sets = {cloud_layer_composite_set_layout(external_background)},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = false,
        .depth_write = false,
    };
}

MaterialPassInfo cloud_layer_temporal_pass_info() {
    return {
        .label = "cloud_temporal",
        .descriptor_sets = {cloud_layer_temporal_set_layout()},
    };
}

RenderGraphTextureState cloud_layer_sampled_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    };
}

RenderGraphTextureDesc cloud_layer_color_texture_desc(std::string label, VkExtent2D extent) {
    return {
        .label = std::move(label),
        .extent = {extent.width, extent.height, 1U},
        .format = kCloudLayerColorFormat,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

CloudLayerQualityBudget cloud_layer_quality_budget(CloudLayerQuality quality) {
    switch (quality) {
    case CloudLayerQuality::Quarter:
        return {.view_steps = 32, .light_steps = 3, .resolution_scale = 0.25F};
    case CloudLayerQuality::Half:
        return {.view_steps = 48, .light_steps = 4, .resolution_scale = 0.5F};
    case CloudLayerQuality::Full:
        return {.view_steps = 64, .light_steps = 6, .resolution_scale = 1.0F};
    }
    return {};
}

VkExtent2D cloud_layer_product_extent(VkExtent2D target_extent, CloudLayerQuality quality) {
    const CloudLayerQualityBudget budget = cloud_layer_quality_budget(quality);
    return {
        .width = std::max(1U, static_cast<std::uint32_t>(
                                  std::round(static_cast<float>(target_extent.width) *
                                             budget.resolution_scale))),
        .height = std::max(1U, static_cast<std::uint32_t>(
                                   std::round(static_cast<float>(target_extent.height) *
                                              budget.resolution_scale))),
    };
}

CloudLayerFrameUniforms cloud_layer_frame_uniforms(const CloudLayerConfig& config,
                                                   const CloudLayerFrameInfo& frame) {
    const float aspect = frame.target_extent.height == 0U
                             ? 1.0F
                             : static_cast<float>(frame.target_extent.width) /
                                   static_cast<float>(frame.target_extent.height);
    const CloudLayerQualityBudget budget = cloud_layer_quality_budget(config.quality);
    const math::Vec3 cloud_top_color{1.12F, 1.04F, 0.82F};
    const math::Vec3 cloud_bottom_color{0.24F, 0.30F, 0.38F};

    return {
        .camera_right_aspect = {frame.camera_right.x, frame.camera_right.y, frame.camera_right.z,
                                aspect},
        .camera_up_tan_half_fovy = {frame.camera_up.x, frame.camera_up.y, frame.camera_up.z,
                                    frame.tan_half_fovy},
        .camera_forward_mode = {frame.camera_forward.x, frame.camera_forward.y,
                                frame.camera_forward.z, frame.camera_mode},
        .camera_position_radius = {frame.camera_position.x, frame.camera_position.y,
                                   frame.camera_position.z, config.planet_radius_m},
        .cloud_shell = {config.bottom_altitude_m,
                        config.top_altitude_m - config.bottom_altitude_m,
                        config.vertical_shear_fraction, cloud_style_value(config.cloud_style)},
        .weather = {config.coverage, config.density, config.weather_scale_km,
                    config.wind_offset_m},
        .sun_direction_intensity = {frame.sun_direction.x, frame.sun_direction.y,
                                    frame.sun_direction.z, frame.sun_intensity},
        .ref_options = {static_cast<float>(static_cast<std::uint32_t>(config.debug_view)),
                        static_cast<float>(budget.view_steps),
                        static_cast<float>(budget.light_steps),
                        static_cast<float>(frame.target_extent.width)},
        .shape_options = {config.crispiness, config.curliness, config.absorption,
                          config.powder_enabled ? 1.0F : 0.0F},
        .weather_feature_weights = {config.weather_fronts, config.weather_cells,
                                    config.weather_streaks, config.detail_erosion},
        .cloud_color_top_shadow = {cloud_top_color.x, cloud_top_color.y, cloud_top_color.z,
                                   config.shadow_strength},
        .cloud_color_bottom_horizon = {cloud_bottom_color.x, cloud_bottom_color.y,
                                       cloud_bottom_color.z, config.horizon_strength},
        .lighting_strengths = {config.ambient_strength, config.direct_strength,
                               config.phase_strength, config.sun_glare_strength},
        .composite_options = {config.resolve_strength, config.final_contrast,
                              config.final_saturation, config.horizon_glow_strength},
        .sampling_options = {sampling_mode_value(config.sampling_mode), config.jitter_strength,
                             config.weather_softness, config.weather_influence},
        .temporal_options = {static_cast<float>(frame.temporal_frame_index % 256U),
                             config.temporal_enabled ? 1.0F : 0.0F, 0.18F, 0.0F},
        .background_options = {background_mode_value(config.background_mode),
                               config.horizon_layer_enabled ? 1.0F : 0.0F,
                               config.local_volume_enabled ? 1.0F : 0.0F, 0.0F},
        .distance_options = {distance_mode_value(config.distance_mode),
                             config.orbit_transition_start_m, config.orbit_transition_end_m,
                             config.orbit_detail_strength},
        .orbit_options = {config.far_shell_start_m, config.far_shell_end_m,
                          config.orbit_density_scale,
                          orbit_representation_value(config.orbit_representation)},
        .orbit_shell_options = {config.orbit_motion_strength, config.orbit_shell_extinction,
                                config.orbit_fill, config.far_shell_strength},
    };
}

CloudLayerWeatherPushConstants cloud_layer_weather_push_constants(
    const CloudLayerConfig& config) {
    return {
        .fronts = config.weather_fronts,
        .cells = config.weather_cells,
        .streaks = config.weather_streaks,
        .cloud_style = cloud_style_value(config.cloud_style),
    };
}

bool cloud_layer_weather_generation_equal(const CloudLayerWeatherPushConstants& lhs,
                                          const CloudLayerWeatherPushConstants& rhs) {
    return lhs.fronts == rhs.fronts && lhs.cells == rhs.cells && lhs.streaks == rhs.streaks &&
           lhs.cloud_style == rhs.cloud_style;
}

CloudLayerGeneratedResources create_cloud_layer_generated_resources(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const CloudLayerGeneratedShaderFiles& shaders, const CloudLayerConfig& config) {
    Texture3D base_noise(device, cloud_layer_volume_texture_config(kCloudLayerBaseNoiseSize));
    Texture3D detail_noise(device, cloud_layer_volume_texture_config(kCloudLayerDetailNoiseSize));
    Texture2D weather(device, cloud_layer_weather_texture_config());

    generate_cloud_storage_volume_texture(
        device, gpu, "cloud layer generate base noise", shaders.base_noise, base_noise,
        ceil_dispatch_groups(kCloudLayerBaseNoiseSize, kCloudLayerBaseNoiseSize,
                             kCloudLayerBaseNoiseSize, kCloudLayerVolumeGroupSize));
    generate_cloud_storage_volume_texture(
        device, gpu, "cloud layer generate detail noise", shaders.detail_noise, detail_noise,
        ceil_dispatch_groups(kCloudLayerDetailNoiseSize, kCloudLayerDetailNoiseSize,
                             kCloudLayerDetailNoiseSize, kCloudLayerVolumeGroupSize));
    const CloudLayerWeatherPushConstants weather_generation =
        cloud_layer_weather_push_constants(config);
    update_cloud_layer_weather_texture(device, gpu, shaders.weather, weather, weather_generation);

    return {
        .base_noise = std::move(base_noise),
        .detail_noise = std::move(detail_noise),
        .weather = std::move(weather),
        .weather_generation = weather_generation,
    };
}

void update_cloud_layer_weather_texture(const cubey::vulkan::Device& device,
                                        cubey::vulkan::GpuRuntime& gpu,
                                        const ShaderStageFile& shader, Texture2D& weather_texture,
                                        CloudLayerWeatherPushConstants push_constants) {
    generate_cloud_storage_texture(
        device, gpu, "cloud layer generate weather", shader, weather_texture.handle(),
        weather_texture.view(), push_constants,
        ceil_dispatch_groups(kCloudLayerWeatherTextureSize, kCloudLayerWeatherTextureSize,
                             kCloudLayerComputeGroupSize));
}

} // namespace cubey::render
