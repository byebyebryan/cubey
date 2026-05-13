#include <cubey/render/generated_texture.h>

#include <cubey/render/pass.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <array>
#include <stdexcept>

namespace cubey::render {

std::uint32_t compute_dispatch_group_count(std::uint32_t extent, std::uint32_t group_size) {
    if (extent == 0 || group_size == 0) {
        throw std::runtime_error("compute dispatch group sizing requires positive values");
    }
    return (extent + group_size - 1U) / group_size;
}

void validate_compute_generated_texture_config(const ComputeGeneratedTexture2DConfig& config) {
    if (config.extent.width == 0 || config.extent.height == 0) {
        throw std::runtime_error("compute generated texture extent must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("compute generated texture format must be defined");
    }
    if (config.shader.path.empty()) {
        throw std::runtime_error("compute generated texture requires a compute shader path");
    }
    if (config.shader.stage != VK_SHADER_STAGE_COMPUTE_BIT) {
        throw std::runtime_error("compute generated texture shader must be a compute stage");
    }
    static_cast<void>(compute_dispatch_group_count(config.extent.width, config.group_size_x));
    static_cast<void>(compute_dispatch_group_count(config.extent.height, config.group_size_y));
    if (config.group_size_z == 0) {
        throw std::runtime_error("compute generated texture z group size must be positive");
    }
}

void validate_compute_generated_texture_format(const cubey::vulkan::Device& device,
                                               const ComputeGeneratedTexture2DConfig& config) {
    validate_compute_generated_texture_config(config);

    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device.physical_device(), config.format, &properties);
    if ((properties.optimalTilingFeatures & config.required_format_features) !=
        config.required_format_features) {
        throw std::runtime_error("compute generated texture format does not support required use");
    }
}

Texture2D create_compute_generated_texture_2d(const cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu,
                                              const ComputeGeneratedTexture2DConfig& config) {
    validate_compute_generated_texture_format(device, config);

    Texture2D texture(device, Texture2DConfig{
                                  .extent = config.extent,
                                  .format = config.format,
                                  .usage = Texture2DUsage::StorageSampled,
                                  .create_sampler = config.create_sampler,
                                  .sampler = config.sampler,
                              });

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;
    descriptor_writes.storage_image(descriptors.set(), 0, texture.view());
    descriptor_writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors.layout()};
    const ComputePipelineResource pipeline(device, ComputePipelineResourceConfig{
                                                       .shader_stage = config.shader,
                                                       .descriptor_set_layouts = set_layouts,
                                                   });

    const std::uint32_t group_count_x =
        compute_dispatch_group_count(config.extent.width, config.group_size_x);
    const std::uint32_t group_count_y =
        compute_dispatch_group_count(config.extent.height, config.group_size_y);

    static_cast<void>(gpu.submit_and_wait(cubey::vulkan::GpuWorkRequest{
        .label = config.label,
        .work =
            [&texture, &pipeline, descriptor_set = descriptors.set(), group_count_x,
             group_count_y](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(texture.handle()));
                record_compute_pipeline_dispatch(recorder, {
                                                               .pipeline = &pipeline,
                                                               .descriptor_set = descriptor_set,
                                                               .group_count_x = group_count_x,
                                                               .group_count_y = group_count_y,
                                                           });
                recorder.transition_image_layout(
                    cubey::vulkan::finish_storage_image_write_for_sampling_transition(
                        texture.handle()));
                commands.submit_and_wait();
            },
    }));

    return texture;
}

} // namespace cubey::render
