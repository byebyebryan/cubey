#include <cubey/render/shadow_map.h>

#include <cubey/vulkan/device.h>

#include <stdexcept>

namespace cubey::render {
namespace {

[[nodiscard]] MaterialPassInfo resolve_material_pass(const ShadowMapPass3DConfig& config) {
    if (config.pipeline.material_pass.label.empty()) {
        return shadow_depth_pass_info();
    }
    if (config.pipeline.material_pass.kind != MaterialPassKind::DepthOnly) {
        throw std::runtime_error("shadow map pass requires a depth-only material pass");
    }
    return config.pipeline.material_pass;
}

[[nodiscard]] VkFormat resolve_depth_format(const cubey::vulkan::Device& device,
                                            VkFormat requested_format) {
    if (requested_format != VK_FORMAT_UNDEFINED) {
        return requested_format;
    }
    return cubey::vulkan::choose_depth_format(device);
}

} // namespace

cubey::vulkan::SamplerConfig shadow_map_sampler_config() noexcept {
    return {
        .min_filter = VK_FILTER_NEAREST,
        .mag_filter = VK_FILTER_NEAREST,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    };
}

DepthTextureConfig shadow_map_depth_texture_config(const ShadowMapPass3DConfig& config,
                                                   VkFormat depth_format) {
    if (config.extent.width == 0 || config.extent.height == 0) {
        throw std::runtime_error("shadow map extent must be nonzero");
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("shadow map depth format must be defined");
    }
    return {
        .extent = config.extent,
        .format = depth_format,
        .create_sampler = true,
        .sampler = config.sampler.value_or(shadow_map_sampler_config()),
    };
}

MaterialPassInfo shadow_depth_pass_info(const ShadowDepthPassInfoConfig& config) {
    return {
        .label = config.label,
        .kind = MaterialPassKind::DepthOnly,
        .push_constants = {config.push_constants.begin(), config.push_constants.end()},
        .cull_mode = config.cull_mode,
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = config.depth_compare_op,
    };
}

ShadowMapPass3D::ShadowMapPass3D(const cubey::vulkan::Device& device,
                                 const ShadowMapPass3DConfig& config)
    : material_pass_(resolve_material_pass(config)),
      depth_texture_(
          device, shadow_map_depth_texture_config(config,
                                                  resolve_depth_format(device,
                                                                       config.depth_format))),
      pipeline_(
          device,
          graphics_pipeline_file_resource_config(
              {
                  .extent = depth_texture_.extent(),
                  .depth_format = depth_texture_.format(),
              },
              {
                  .shader_stage_files = config.pipeline.shader_stage_files,
                  .vertex_bindings = config.pipeline.vertex_bindings,
                  .vertex_attributes = config.pipeline.vertex_attributes,
                  .descriptor_set_layouts = config.pipeline.descriptor_set_layouts,
                  .material_pass = material_pass_,
              })) {}

} // namespace cubey::render
