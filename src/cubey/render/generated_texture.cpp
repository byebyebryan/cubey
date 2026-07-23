#include <cubey/render/generated_texture.h>

#include <cubey/render/pass.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <array>
#include <optional>
#include <stdexcept>
#include <vector>

namespace cubey::render {

std::uint32_t compute_dispatch_group_count(std::uint32_t extent, std::uint32_t group_size) {
    return ceil_dispatch_group_count(extent, group_size);
}

void validate_compute_generated_texture_config(const ComputeGeneratedTexture2DConfig& config) {
    if (config.extent.width == 0 || config.extent.height == 0) {
        throw std::runtime_error("compute generated texture extent must be nonzero");
    }
    if (config.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("compute generated texture format must be defined");
    }
    if (config.mip_levels == 0) {
        throw std::runtime_error("compute generated texture mip count must be nonzero");
    }
    if (config.create_sampler && config.mip_levels > 1 &&
        config.sampler.max_lod < static_cast<float>(config.mip_levels - 1U)) {
        throw std::runtime_error(
            "compute generated texture sampler must expose every generated mip");
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
    if ((config.push_constants.size() % 4U) != 0U) {
        throw std::runtime_error("compute generated texture push constants must be aligned");
    }
}

void validate_compute_generated_texture_format(const cubey::vulkan::Device& device,
                                               const ComputeGeneratedTexture2DConfig& config) {
    validate_compute_generated_texture_config(config);

    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device.physical_device(), config.format, &properties);
    VkFormatFeatureFlags required = config.required_format_features;
    if (config.mip_levels > 1) {
        required |= VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    }
    if ((properties.optimalTilingFeatures & required) != required) {
        throw std::runtime_error("compute generated texture format does not support required use");
    }
}

Texture2D create_compute_generated_texture_2d(const cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuOwnerContext& context,
                                              const ComputeGeneratedTexture2DConfig& config) {
    validate_compute_generated_texture_format(device, config);

    Texture2D texture(device, Texture2DConfig{
                                  .extent = config.extent,
                                  .mip_levels = config.mip_levels,
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
    cubey::vulkan::ImageView storage_view(device, cubey::vulkan::ImageViewConfig{
                                                      .image = texture.handle(),
                                                      .format = texture.format(),
                                                      .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                                      .view_type = VK_IMAGE_VIEW_TYPE_2D,
                                                      .base_mip_level = 0,
                                                      .level_count = 1,
                                                  });
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;
    descriptor_writes.storage_image(descriptors.set(), 0, storage_view.handle());
    descriptor_writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors.layout()};
    std::vector<VkPushConstantRange> push_constant_ranges;
    if (!config.push_constants.empty()) {
        push_constant_ranges.push_back({
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = static_cast<std::uint32_t>(config.push_constants.size()),
        });
    }
    const ComputePipelineResource pipeline(device, ComputePipelineResourceConfig{
                                                       .shader_stage = config.shader,
                                                       .descriptor_set_layouts = set_layouts,
                                                       .push_constants = push_constant_ranges,
                                                   });

    const std::uint32_t group_count_x =
        compute_dispatch_group_count(config.extent.width, config.group_size_x);
    const std::uint32_t group_count_y =
        compute_dispatch_group_count(config.extent.height, config.group_size_y);

    const std::vector<std::byte> push_constants(config.push_constants.begin(),
                                                config.push_constants.end());
    cubey::vulkan::ImmediateCommands commands(context);
    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
    recorder.transition_image_layout(
        cubey::vulkan::begin_storage_image_write_transition(texture.handle()));
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptors.set());
    if (!push_constants.empty()) {
        recorder.push_constants_bytes(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                      static_cast<std::uint32_t>(push_constants.size()),
                                      push_constants.data());
    }
    recorder.dispatch(group_count_x, group_count_y, 1U);
    if (texture.mip_levels() > 1) {
        record_generate_texture_2d_mips(commands.command_buffer(), texture,
                                        VK_IMAGE_LAYOUT_GENERAL);
    } else {
        recorder.transition_image_layout(
            cubey::vulkan::finish_storage_image_write_for_sampling_transition(texture.handle()));
    }
    commands.submit_and_wait();

    return texture;
}

Texture2D create_compute_generated_texture_2d(const cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu,
                                              const ComputeGeneratedTexture2DConfig& config) {
    std::optional<Texture2D> texture;
    static_cast<void>(gpu.submit_and_wait(cubey::vulkan::GpuWorkRequest{
        .label = config.label,
        .work =
            [&device, &config, &texture](cubey::vulkan::GpuOwnerContext& context) {
                texture.emplace(create_compute_generated_texture_2d(device, context, config));
            },
    }));
    return std::move(texture).value();
}

} // namespace cubey::render
