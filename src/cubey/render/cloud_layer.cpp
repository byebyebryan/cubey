#include <cubey/render/cloud_layer.h>

#include <cubey/render/render_graph.h>

#include <algorithm>
#include <cmath>

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

} // namespace cubey::render
