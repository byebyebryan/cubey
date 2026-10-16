#include <cubey/render/cloud_layer.h>

#include <cubey/render/generated_texture.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::render {

namespace {

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

ComputeGeneratedTexture2DConfig cloud_layer_blue_noise_texture_config(ShaderStageFile shader) {
    return {
        .label = "cloud layer generate blue noise",
        .extent = {kCloudLayerBlueNoiseTextureSize, kCloudLayerBlueNoiseTextureSize},
        .format = kCloudLayerNoiseFormat,
        .shader = std::move(shader),
        .group_size_x = kCloudLayerComputeGroupSize,
        .group_size_y = kCloudLayerComputeGroupSize,
        .group_size_z = 1,
        .create_sampler = true,
        .sampler = cloud_layer_repeat_sampler_config(),
        .required_format_features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
    };
}

CloudLayerGeneratedResources create_cloud_layer_generated_resources(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const CloudLayerGeneratedShaderFiles& shaders, const CloudLayerConfig& config) {
    Texture3D base_noise(device, cloud_layer_volume_texture_config(kCloudLayerBaseNoiseSize));
    Texture3D detail_noise(device, cloud_layer_volume_texture_config(kCloudLayerDetailNoiseSize));
    Texture2D weather(device, cloud_layer_weather_texture_config());
    Texture2D blue_noise =
        create_compute_generated_texture_2d(device, gpu,
                                            cloud_layer_blue_noise_texture_config(
                                                shaders.blue_noise));

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
        .blue_noise = std::move(blue_noise),
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

void CloudLayerRuntime::create_generated_resources(const cubey::vulkan::Device& device,
                                                   cubey::vulkan::GpuRuntime& gpu,
                                                   const CloudLayerGeneratedShaderFiles& shaders,
                                                   const CloudLayerConfig& config) {
    generated_.emplace(cubey::render::create_cloud_layer_generated_resources(device, gpu, shaders,
                                                                             config));
}

void CloudLayerRuntime::destroy_generated_resources() {
    generated_.reset();
}

void CloudLayerRuntime::update_weather_texture(const cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu,
                                               const ShaderStageFile& shader,
                                               const CloudLayerConfig& config, bool force) {
    if (!generated_.has_value()) {
        throw std::runtime_error("cloud layer generated resources are not initialized");
    }
    const CloudLayerWeatherPushConstants current = cloud_layer_weather_push_constants(config);
    if (!force && cloud_layer_weather_generation_equal(generated_->weather_generation, current)) {
        return;
    }
    cubey::render::update_cloud_layer_weather_texture(device, gpu, shader, generated_->weather,
                                                      current);
    generated_->weather_generation = current;
}

const CloudLayerGeneratedResources& CloudLayerRuntime::generated_resources() const {
    if (!generated_.has_value()) {
        throw std::runtime_error("cloud layer generated resources are not initialized");
    }
    return generated_.value();
}


} // namespace cubey::render
