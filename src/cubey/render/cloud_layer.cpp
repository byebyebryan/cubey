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
                vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerBlueNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
            },
    };
}

[[nodiscard]] bool cloud_layer_composite_mode_has_background(CloudLayerCompositeMode mode) {
    return mode == CloudLayerCompositeMode::ExternalBackground ||
           mode == CloudLayerCompositeMode::ExternalBackgroundSceneDepth;
}

[[nodiscard]] bool cloud_layer_composite_mode_has_scene_depth(CloudLayerCompositeMode mode) {
    return mode == CloudLayerCompositeMode::ExternalBackgroundSceneDepth;
}

[[nodiscard]] MaterialDescriptorSetLayout cloud_layer_composite_set_layout(
    bool external_background, bool scene_depth) {
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
    if (scene_depth) {
        layout.bindings.push_back(vulkan::DescriptorSetBindingConfig{
            .binding = kCloudLayerCompositeSceneDepthBinding,
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

[[nodiscard]] float view_sample_mode_value(CloudLayerViewSampleMode mode) {
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

[[nodiscard]] float density_model_value(CloudLayerDensityModel model) {
    return static_cast<float>(static_cast<std::uint32_t>(model));
}

[[nodiscard]] float resolve_mode_value(CloudLayerResolveMode mode) {
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

MaterialPassInfo cloud_layer_march_pass_info() {
    return {
        .label = "cloud_march",
        .descriptor_sets = {cloud_layer_march_set_layout()},
    };
}

MaterialPassInfo cloud_layer_composite_pass_info(bool external_background, bool scene_depth) {
    return {
        .label = external_background ? (scene_depth ? "cloud_composite_background_depth"
                                                     : "cloud_composite_background")
                                     : "cloud_composite",
        .descriptor_sets = {cloud_layer_composite_set_layout(external_background, scene_depth)},
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

CloudLayerDebugView cloud_layer_debug_view_from_name(std::string_view value) {
    if (value.empty() || value == "final") {
        return CloudLayerDebugView::Final;
    }
    if (value == "raw-final") {
        return CloudLayerDebugView::RawFinal;
    }
    if (value == "authored-weather" || value == "weather") {
        return CloudLayerDebugView::AuthoredWeather;
    }
    if (value == "density") {
        return CloudLayerDebugView::Density;
    }
    if (value == "transmittance") {
        return CloudLayerDebugView::Transmittance;
    }
    if (value == "lighting") {
        return CloudLayerDebugView::Lighting;
    }
    if (value == "ambient-light") {
        return CloudLayerDebugView::AmbientLight;
    }
    if (value == "direct-light") {
        return CloudLayerDebugView::DirectLight;
    }
    if (value == "phase-light") {
        return CloudLayerDebugView::PhaseLight;
    }
    if (value == "shadow") {
        return CloudLayerDebugView::Shadow;
    }
    if (value == "steps") {
        return CloudLayerDebugView::Steps;
    }
    if (value == "background") {
        return CloudLayerDebugView::Background;
    }
    if (value == "cloud-alpha") {
        return CloudLayerDebugView::CloudAlpha;
    }
    if (value == "distance") {
        return CloudLayerDebugView::Distance;
    }
    if (value == "metadata-distance") {
        return CloudLayerDebugView::MetadataDistance;
    }
    if (value == "metadata-alpha") {
        return CloudLayerDebugView::MetadataAlpha;
    }
    if (value == "metadata-confidence") {
        return CloudLayerDebugView::MetadataConfidence;
    }
    if (value == "metadata-density") {
        return CloudLayerDebugView::MetadataDensity;
    }
    if (value == "base-density") {
        return CloudLayerDebugView::BaseDensity;
    }
    if (value == "detail-density") {
        return CloudLayerDebugView::DetailDensity;
    }
    if (value == "cloud-type") {
        return CloudLayerDebugView::CloudType;
    }
    if (value == "local-scatter") {
        return CloudLayerDebugView::LocalScatter;
    }
    if (value == "local-clear") {
        return CloudLayerDebugView::LocalClear;
    }
    if (value == "local-structure") {
        return CloudLayerDebugView::LocalStructure;
    }
    if (value == "local-edge-detail") {
        return CloudLayerDebugView::LocalEdgeDetail;
    }
    if (value == "weather-edge") {
        return CloudLayerDebugView::WeatherEdge;
    }
    if (value == "coverage-bias" || value == "weather-bias" || value == "weather-mask") {
        return CloudLayerDebugView::CoverageBias;
    }
    if (value == "visible-density") {
        return CloudLayerDebugView::VisibleDensity;
    }
    if (value == "visible-cloud-type") {
        return CloudLayerDebugView::VisibleCloudType;
    }
    if (value == "distance-regime") {
        return CloudLayerDebugView::DistanceRegime;
    }
    if (value == "transition-weights") {
        return CloudLayerDebugView::TransitionWeights;
    }
    if (value == "scene-depth-occlusion" || value == "depth-occlusion" ||
        value == "depth-mask") {
        return CloudLayerDebugView::SceneDepthOcclusion;
    }
    if (value == "local-alpha") {
        return CloudLayerDebugView::LocalAlpha;
    }
    if (value == "far-shell-alpha") {
        return CloudLayerDebugView::FarShellAlpha;
    }
    if (value == "local-with-shell-alpha") {
        return CloudLayerDebugView::LocalWithShellAlpha;
    }
    if (value == "orbit-alpha") {
        return CloudLayerDebugView::OrbitAlpha;
    }
    if (value == "orbit-weather" || value == "orbit-coverage") {
        return CloudLayerDebugView::OrbitCoverage;
    }
    if (value == "orbit-detail") {
        return CloudLayerDebugView::OrbitDetail;
    }
    if (value == "orbit-hull") {
        return CloudLayerDebugView::OrbitHull;
    }
    if (value == "orbit-envelope") {
        return CloudLayerDebugView::OrbitEnvelope;
    }
    if (value == "orbit-shell-alpha" || value == "shell-alpha") {
        return CloudLayerDebugView::OrbitShellAlpha;
    }
    if (value == "orbit-shell-height" || value == "shell-height") {
        return CloudLayerDebugView::OrbitShellHeight;
    }
    if (value == "orbit-shell-normal" || value == "shell-normal") {
        return CloudLayerDebugView::OrbitShellNormal;
    }
    if (value == "orbit-shell-shadow" || value == "shell-shadow") {
        return CloudLayerDebugView::OrbitShellShadow;
    }
    if (value == "orbit-shell-footprint" || value == "shell-footprint") {
        return CloudLayerDebugView::OrbitShellFootprint;
    }
    if (value == "orbit-shell-filter" || value == "shell-filter") {
        return CloudLayerDebugView::OrbitShellFilter;
    }
    if (value == "orbit-shell-mass" || value == "shell-mass") {
        return CloudLayerDebugView::OrbitShellMass;
    }
    if (value == "jitter-pattern" || value == "jitter") {
        return CloudLayerDebugView::JitterPattern;
    }
    if (value == "horizon-step-length" || value == "horizon-steps") {
        return CloudLayerDebugView::HorizonStepLength;
    }
    if (value == "horizon-filter-lod" || value == "horizon-lod") {
        return CloudLayerDebugView::HorizonFilterLod;
    }
    if (value == "horizon-handoff" || value == "horizon-gate") {
        return CloudLayerDebugView::HorizonHandoff;
    }
    if (value == "local-truncation" || value == "horizon-local-truncation") {
        return CloudLayerDebugView::LocalTruncation;
    }
    if (value == "integrated-horizon-alpha" || value == "horizon-integrated-alpha") {
        return CloudLayerDebugView::IntegratedHorizonAlpha;
    }
    if (value == "integrated-horizon-radiance" || value == "horizon-integrated-radiance") {
        return CloudLayerDebugView::IntegratedHorizonRadiance;
    }
    if (value == "edge-mask" || value == "resolve-edge") {
        return CloudLayerDebugView::EdgeMask;
    }
    throw std::runtime_error("unknown cloud debug view: " + std::string(value));
}

const char* cloud_layer_debug_view_name(CloudLayerDebugView view) {
    switch (view) {
    case CloudLayerDebugView::Final:
        return "final";
    case CloudLayerDebugView::RawFinal:
        return "raw-final";
    case CloudLayerDebugView::AuthoredWeather:
        return "authored-weather";
    case CloudLayerDebugView::Density:
        return "density";
    case CloudLayerDebugView::Transmittance:
        return "transmittance";
    case CloudLayerDebugView::Lighting:
        return "lighting";
    case CloudLayerDebugView::AmbientLight:
        return "ambient-light";
    case CloudLayerDebugView::DirectLight:
        return "direct-light";
    case CloudLayerDebugView::PhaseLight:
        return "phase-light";
    case CloudLayerDebugView::Shadow:
        return "shadow";
    case CloudLayerDebugView::Steps:
        return "steps";
    case CloudLayerDebugView::Background:
        return "background";
    case CloudLayerDebugView::CloudAlpha:
        return "cloud-alpha";
    case CloudLayerDebugView::Distance:
        return "distance";
    case CloudLayerDebugView::MetadataDistance:
        return "metadata-distance";
    case CloudLayerDebugView::MetadataAlpha:
        return "metadata-alpha";
    case CloudLayerDebugView::MetadataConfidence:
        return "metadata-confidence";
    case CloudLayerDebugView::MetadataDensity:
        return "metadata-density";
    case CloudLayerDebugView::BaseDensity:
        return "base-density";
    case CloudLayerDebugView::DetailDensity:
        return "detail-density";
    case CloudLayerDebugView::CloudType:
        return "cloud-type";
    case CloudLayerDebugView::LocalScatter:
        return "local-scatter";
    case CloudLayerDebugView::LocalClear:
        return "local-clear";
    case CloudLayerDebugView::LocalStructure:
        return "local-structure";
    case CloudLayerDebugView::LocalEdgeDetail:
        return "local-edge-detail";
    case CloudLayerDebugView::WeatherEdge:
        return "weather-edge";
    case CloudLayerDebugView::CoverageBias:
        return "coverage-bias";
    case CloudLayerDebugView::VisibleDensity:
        return "visible-density";
    case CloudLayerDebugView::VisibleCloudType:
        return "visible-cloud-type";
    case CloudLayerDebugView::DistanceRegime:
        return "distance-regime";
    case CloudLayerDebugView::TransitionWeights:
        return "transition-weights";
    case CloudLayerDebugView::SceneDepthOcclusion:
        return "scene-depth-occlusion";
    case CloudLayerDebugView::LocalAlpha:
        return "local-alpha";
    case CloudLayerDebugView::FarShellAlpha:
        return "far-shell-alpha";
    case CloudLayerDebugView::LocalWithShellAlpha:
        return "local-with-shell-alpha";
    case CloudLayerDebugView::OrbitAlpha:
        return "orbit-alpha";
    case CloudLayerDebugView::OrbitCoverage:
        return "orbit-coverage";
    case CloudLayerDebugView::OrbitDetail:
        return "orbit-detail";
    case CloudLayerDebugView::OrbitHull:
        return "orbit-hull";
    case CloudLayerDebugView::OrbitEnvelope:
        return "orbit-envelope";
    case CloudLayerDebugView::OrbitShellAlpha:
        return "orbit-shell-alpha";
    case CloudLayerDebugView::OrbitShellHeight:
        return "orbit-shell-height";
    case CloudLayerDebugView::OrbitShellNormal:
        return "orbit-shell-normal";
    case CloudLayerDebugView::OrbitShellShadow:
        return "orbit-shell-shadow";
    case CloudLayerDebugView::OrbitShellFootprint:
        return "orbit-shell-footprint";
    case CloudLayerDebugView::OrbitShellFilter:
        return "orbit-shell-filter";
    case CloudLayerDebugView::OrbitShellMass:
        return "orbit-shell-mass";
    case CloudLayerDebugView::JitterPattern:
        return "jitter-pattern";
    case CloudLayerDebugView::HorizonStepLength:
        return "horizon-step-length";
    case CloudLayerDebugView::HorizonFilterLod:
        return "horizon-filter-lod";
    case CloudLayerDebugView::HorizonHandoff:
        return "horizon-handoff";
    case CloudLayerDebugView::LocalTruncation:
        return "local-truncation";
    case CloudLayerDebugView::IntegratedHorizonAlpha:
        return "integrated-horizon-alpha";
    case CloudLayerDebugView::IntegratedHorizonRadiance:
        return "integrated-horizon-radiance";
    case CloudLayerDebugView::EdgeMask:
        return "edge-mask";
    }
    return "final";
}

CloudLayerDebugView next_cloud_layer_debug_view(CloudLayerDebugView view) {
    const auto it = std::find(kCloudLayerDebugViews.begin(), kCloudLayerDebugViews.end(), view);
    if (it == kCloudLayerDebugViews.end() || std::next(it) == kCloudLayerDebugViews.end()) {
        return kCloudLayerDebugViews.front();
    }
    return *std::next(it);
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
    const std::int32_t view_steps =
        config.view_steps_override > 0 ? std::clamp(config.view_steps_override, 1, 128)
                                       : budget.view_steps;
    const std::int32_t view_samples = config.view_samples >= 4 ? 4
                                     : config.view_samples >= 2 ? 2
                                                               : 1;
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
        .sun_color = {frame.sun_color.x, frame.sun_color.y, frame.sun_color.z, 0.0F},
        .moon_direction_intensity = {frame.moon_direction.x, frame.moon_direction.y,
                                     frame.moon_direction.z, frame.moon_intensity},
        .moon_color = {frame.moon_color.x, frame.moon_color.y, frame.moon_color.z, 0.0F},
        .ambient_color_intensity = {frame.ambient_color.x, frame.ambient_color.y,
                                    frame.ambient_color.z, frame.ambient_intensity},
        .ref_options = {static_cast<float>(static_cast<std::uint32_t>(config.debug_view)),
                        static_cast<float>(view_steps),
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
                             config.temporal_enabled ? 1.0F : 0.0F, 0.14F,
                             view_sample_mode_value(config.view_sample_mode)},
        .background_options = {background_mode_value(config.background_mode),
                               config.horizon_layer_enabled ? 1.0F : 0.0F,
                               config.local_volume_enabled ? 1.0F : 0.0F,
                               frame.external_background ? 1.0F : 0.0F},
        .distance_options = {distance_mode_value(config.distance_mode),
                             config.orbit_transition_start_m, config.orbit_transition_end_m,
                             config.orbit_detail_strength},
        .orbit_options = {config.far_shell_start_m, config.far_shell_end_m,
                          config.orbit_density_scale,
                          orbit_representation_value(config.orbit_representation)},
        .orbit_shell_options = {config.orbit_motion_strength, config.orbit_shell_extinction,
                                config.orbit_fill, config.far_shell_strength},
        .scene_depth_options = {std::max(frame.near_plane_m, 0.0001F),
                                std::max(frame.far_plane_m,
                                         std::max(frame.near_plane_m, 0.0001F) + 1.0F),
                                frame.scene_depth_occlusion_enabled ? 1.0F : 0.0F,
                                std::max(frame.scene_depth_fade_m, 1.0F)},
        .density_options = {density_model_value(config.density_model), config.shape_domain_km,
                            config.footprint_filter_strength, static_cast<float>(view_samples)},
        .edge_options = {config.edge_softness, config.edge_detail_fade,
                         config.edge_resolve_strength, resolve_mode_value(config.resolve_mode)},
    };
}

CloudLayerViewRegime cloud_layer_view_regime(const CloudLayerViewRegimeInput& input) {
    const float radius = std::max(input.planet_radius_m, 1.0F);
    const float camera_radius = glm::length(input.camera_position);
    const float altitude = std::max(camera_radius - radius, 0.0F);
    const float transition_start = std::max(input.orbit_transition_start_m, 1.0F);
    const float transition_end = std::max(input.orbit_transition_end_m, transition_start + 1.0F);
    const float altitude_blend =
        std::clamp((altitude - transition_start) / (transition_end - transition_start), 0.0F,
                   1.0F);

    math::Vec3 up{0.0F, 1.0F, 0.0F};
    if (camera_radius > 0.0001F) {
        up = input.camera_position / camera_radius;
    }
    math::Vec3 forward{0.0F, 0.0F, -1.0F};
    if (glm::dot(input.camera_forward, input.camera_forward) > 0.000001F) {
        forward = glm::normalize(input.camera_forward);
    }
    const float view_up = std::abs(glm::dot(forward, up));
    const float horizon_grazing = 1.0F - std::clamp(view_up / 0.28F, 0.0F, 1.0F);

    float camera_mode = 0.0F;
    if (altitude_blend >= 0.95F) {
        camera_mode = 4.0F;
    } else if (altitude_blend >= 0.05F || (altitude >= 5000.0F && horizon_grazing > 0.75F)) {
        camera_mode = 1.0F;
    }

    return {
        .camera_mode = camera_mode,
        .altitude_m = altitude,
        .altitude_blend = altitude_blend,
        .horizon_grazing = horizon_grazing,
    };
}

CloudLayerWeatherPushConstants cloud_layer_weather_push_constants(
    const CloudLayerConfig& config) {
    return {
        .fronts = config.weather_fronts,
        .cells = config.weather_cells,
        .streaks = config.weather_streaks,
        .cloud_style = cloud_style_value(config.cloud_style),
        .density_model = density_model_value(config.density_model),
    };
}

bool cloud_layer_weather_generation_equal(const CloudLayerWeatherPushConstants& lhs,
                                          const CloudLayerWeatherPushConstants& rhs) {
    return lhs.fronts == rhs.fronts && lhs.cells == rhs.cells && lhs.streaks == rhs.streaks &&
           lhs.cloud_style == rhs.cloud_style && lhs.density_model == rhs.density_model;
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

namespace {

[[nodiscard]] Texture2DConfig cloud_layer_history_texture_config(VkExtent2D extent) {
    return {
        .extent = extent,
        .format = kCloudLayerColorFormat,
        .usage = Texture2DUsage::StorageSampled,
        .create_sampler = false,
        .sampler = {},
    };
}

[[nodiscard]] bool cloud_layer_near(float lhs, float rhs, float tolerance = 0.001F) {
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] bool cloud_layer_near(math::Vec4 lhs, math::Vec4 rhs,
                                    float tolerance = 0.001F) {
    return cloud_layer_near(lhs.x, rhs.x, tolerance) &&
           cloud_layer_near(lhs.y, rhs.y, tolerance) &&
           cloud_layer_near(lhs.z, rhs.z, tolerance) &&
           cloud_layer_near(lhs.w, rhs.w, tolerance);
}

[[nodiscard]] bool cloud_layer_history_uniforms_compatible(
    const CloudLayerFrameUniforms& previous, const CloudLayerFrameUniforms& current) {
    const math::Vec4 previous_weather_static{
        previous.weather.x,
        previous.weather.y,
        previous.weather.z,
        0.0F,
    };
    const math::Vec4 current_weather_static{
        current.weather.x,
        current.weather.y,
        current.weather.z,
        0.0F,
    };
    const math::Vec4 previous_temporal_static{
        0.0F,
        0.0F,
        0.0F,
        previous.temporal_options.w,
    };
    const math::Vec4 current_temporal_static{
        0.0F,
        0.0F,
        0.0F,
        current.temporal_options.w,
    };
    return cloud_layer_near(previous.cloud_shell, current.cloud_shell) &&
           cloud_layer_near(previous_weather_static, current_weather_static) &&
           cloud_layer_near(previous.sun_direction_intensity, current.sun_direction_intensity) &&
           cloud_layer_near(previous.sun_color, current.sun_color) &&
           cloud_layer_near(previous.moon_direction_intensity,
                            current.moon_direction_intensity) &&
           cloud_layer_near(previous.moon_color, current.moon_color) &&
           cloud_layer_near(previous.ambient_color_intensity,
                            current.ambient_color_intensity) &&
           cloud_layer_near(previous.ref_options, current.ref_options) &&
           cloud_layer_near(previous.shape_options, current.shape_options) &&
           cloud_layer_near(previous.weather_feature_weights, current.weather_feature_weights) &&
           cloud_layer_near(previous.cloud_color_top_shadow, current.cloud_color_top_shadow) &&
           cloud_layer_near(previous.cloud_color_bottom_horizon,
                            current.cloud_color_bottom_horizon) &&
           cloud_layer_near(previous.lighting_strengths, current.lighting_strengths) &&
           cloud_layer_near(previous.composite_options, current.composite_options) &&
           cloud_layer_near(previous.sampling_options, current.sampling_options) &&
           cloud_layer_near(previous_temporal_static, current_temporal_static) &&
           cloud_layer_near(previous.background_options, current.background_options) &&
           cloud_layer_near(previous.distance_options, current.distance_options) &&
           cloud_layer_near(previous.orbit_options, current.orbit_options) &&
           cloud_layer_near(previous.orbit_shell_options, current.orbit_shell_options) &&
           cloud_layer_near(previous.scene_depth_options, current.scene_depth_options) &&
           cloud_layer_near(previous.density_options, current.density_options) &&
           cloud_layer_near(previous.edge_options, current.edge_options);
}

[[nodiscard]] bool cloud_layer_extent_equal(VkExtent2D lhs, VkExtent2D rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

} // namespace

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

void CloudLayerRuntime::destroy_swapchain_resources() {
    composite_sampler_.reset();
    temporal_material_.reset();
    march_material_.reset();
    composite_material_.reset();
    temporal_pipeline_.reset();
    march_pipeline_.reset();
    composite_pipeline_.reset();
    history_cloud_textures_.clear();
    history_metadata_textures_.clear();
    history_read_indices_.clear();
    history_texture_valid_.clear();
    history_frame_states_.clear();
    temporal_uniforms_.reset();
    frame_uniforms_.reset();
}

void CloudLayerRuntime::create_swapchain_resources(
    const cubey::vulkan::Device& device, const CloudLayerRuntimeShaderFiles& shaders,
    CloudLayerCompositeMode composite_mode, VkFormat color_format, VkExtent2D extent,
    std::uint32_t frame_slot_count, const CloudLayerConfig& config) {
    destroy_swapchain_resources();
    composite_mode_ = composite_mode;
    frame_uniforms_.emplace(device, frame_slot_count);
    temporal_uniforms_.emplace(device, frame_slot_count);

    const MaterialPassInfo march_pass = cloud_layer_march_pass_info();
    march_material_.emplace(device, MaterialInstanceConfig{
                                        .material_pass = march_pass,
                                        .descriptor_set = 0,
                                        .set_count = frame_slot_count,
                                    });
    const std::array<VkDescriptorSetLayout, 1> march_layouts{march_material_->layout()};
    march_pipeline_.emplace(device, ComputePipelineResourceConfig{
                                        .shader_stage = shaders.march,
                                        .descriptor_set_layouts = march_layouts,
                                    });

    const MaterialPassInfo temporal_pass = cloud_layer_temporal_pass_info();
    temporal_material_.emplace(device, MaterialInstanceConfig{
                                           .material_pass = temporal_pass,
                                           .descriptor_set = 0,
                                           .set_count = frame_slot_count,
                                       });
    const std::array<VkDescriptorSetLayout, 1> temporal_layouts{temporal_material_->layout()};
    temporal_pipeline_.emplace(device, ComputePipelineResourceConfig{
                                           .shader_stage = shaders.temporal,
                                           .descriptor_set_layouts = temporal_layouts,
                                       });
    create_history_textures(device, cloud_layer_product_extent(extent, config.quality),
                            frame_slot_count);

    const bool external_background = cloud_layer_composite_mode_has_background(composite_mode);
    const bool scene_depth = cloud_layer_composite_mode_has_scene_depth(composite_mode);
    const MaterialPassInfo composite_pass =
        cloud_layer_composite_pass_info(external_background, scene_depth);
    composite_material_.emplace(device, MaterialInstanceConfig{
                                            .material_pass = composite_pass,
                                            .descriptor_set = 0,
                                            .set_count = frame_slot_count,
                                        });
    composite_sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                           .min_filter = VK_FILTER_LINEAR,
                                           .mag_filter = VK_FILTER_LINEAR,
                                           .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                       });
    const std::array<VkDescriptorSetLayout, 1> composite_layouts{
        composite_material_->layout()};
    const std::array<ShaderStageFile, 2> composite_shaders{
        shaders.composite_vertex,
        shaders.composite_fragment,
    };
    composite_pipeline_.emplace(device, GraphicsPipelineFileResourceConfig{
                                            .extent = extent,
                                            .color_format = color_format,
                                            .shader_stage_files = composite_shaders,
                                            .descriptor_set_layouts = composite_layouts,
                                            .material_pass = composite_pass,
                                        });
}

const CloudLayerGeneratedResources& CloudLayerRuntime::generated_resources() const {
    if (!generated_.has_value()) {
        throw std::runtime_error("cloud layer generated resources are not initialized");
    }
    return generated_.value();
}

void CloudLayerRuntime::upload_frame_uniforms(FrameSlot frame_slot,
                                             const CloudLayerFrameUniforms& uniforms) const {
    frame_uniforms().upload(frame_slot, uniforms);
}

CloudLayerRuntimeFrame CloudLayerRuntime::declare_product(
    RenderGraphBuilder& graph, VkExtent2D target_extent, const CloudLayerConfig& config,
    FrameSlot frame_slot, CloudLayerFrameUniforms uniforms) const {
    const VkExtent2D cloud_extent = cloud_layer_product_extent(target_extent, config.quality);
    const bool temporal_pass_enabled =
        config.temporal_enabled && config.debug_view == CloudLayerDebugView::Final &&
        history_extent_matches(cloud_extent);
    const std::uint32_t history_read_index =
        frame_slot.index < history_read_indices_.size() ? history_read_indices_[frame_slot.index]
                                                        : 0U;
    const std::uint32_t history_write_index = 1U - history_read_index;
    const bool history_read_valid = history_texture_valid(frame_slot, history_read_index);
    const bool history_write_valid = history_texture_valid(frame_slot, history_write_index);
    const RenderGraphTextureState sampled_state = cloud_layer_sampled_texture_state();

    CloudLayerRuntimeFrame frame{
        .product =
            {
                .cloud = graph.create_texture(
                    cloud_layer_color_texture_desc("cloud product", cloud_extent)),
                .metadata = graph.create_texture(
                    cloud_layer_color_texture_desc("cloud metadata", cloud_extent)),
                .extent = cloud_extent,
            },
        .temporal_pass_enabled = temporal_pass_enabled,
        .history_write_index = history_write_index,
        .frame_uniforms = uniforms,
    };
    frame.product.resolved_cloud = frame.product.cloud;
    frame.product.resolved_metadata = frame.product.metadata;

    graph.add_pass("cloud march", RenderGraphQueueDomain::Compute)
        .write_storage_texture(frame.product.cloud, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write_storage_texture(frame.product.metadata, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .execute([this, frame_slot, cloud_extent](
                     const RenderGraphExecutionContext& context) {
            record_march_dispatch(context.recorder(), march_material().set(frame_slot),
                                  cloud_extent);
        });

    if (temporal_pass_enabled) {
        const Texture2D& read_texture = history_cloud_texture(frame_slot, history_read_index);
        frame.history_cloud_read = graph.import_texture(
            cloud_layer_color_texture_desc("cloud history read", cloud_extent),
            read_texture.handle(), read_texture.view(),
            history_read_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        const Texture2D& metadata_read_texture =
            history_metadata_texture(frame_slot, history_read_index);
        frame.history_metadata_read = graph.import_texture(
            cloud_layer_color_texture_desc("cloud metadata history read", cloud_extent),
            metadata_read_texture.handle(), metadata_read_texture.view(),
            history_read_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        const Texture2D& write_texture = history_cloud_texture(frame_slot, history_write_index);
        frame.history_cloud_write = graph.import_texture(
            cloud_layer_color_texture_desc("cloud history write", cloud_extent),
            write_texture.handle(), write_texture.view(),
            history_write_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        const Texture2D& metadata_write_texture =
            history_metadata_texture(frame_slot, history_write_index);
        frame.history_metadata_write = graph.import_texture(
            cloud_layer_color_texture_desc("cloud metadata history write", cloud_extent),
            metadata_write_texture.handle(), metadata_write_texture.view(),
            history_write_valid
                ? std::optional<RenderGraphTextureState>{sampled_state}
                : std::optional<RenderGraphTextureState>{render_graph_undefined_texture_state()},
            sampled_state);
        graph.add_pass("cloud temporal", RenderGraphQueueDomain::Compute)
            .read_texture(frame.product.cloud, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_texture(frame.product.metadata, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_texture(frame.history_cloud_read, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .read_texture(frame.history_metadata_read, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .write_storage_texture(frame.history_cloud_write, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .write_storage_texture(frame.history_metadata_write,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .execute([this, frame_slot, cloud_extent, uniforms, history_read_index](
                         const RenderGraphExecutionContext& context) {
                temporal_uniforms().upload(frame_slot, temporal_frame_uniforms(
                                                           frame_slot, uniforms,
                                                           history_read_index));
                record_temporal_dispatch(context.recorder(), temporal_material().set(frame_slot),
                                         cloud_extent);
            });
        frame.product.resolved_cloud = frame.history_cloud_write;
        frame.product.resolved_metadata = frame.history_metadata_write;
    }

    return frame;
}

void CloudLayerRuntime::declare_composite(
    RenderGraphBuilder& graph, RenderGraphTextureHandle target, const CloudLayerRuntimeFrame& frame,
    FrameSlot frame_slot, std::optional<RenderGraphTextureHandle> background,
    std::optional<RenderGraphTextureHandle> scene_depth) const {
    const bool expects_background = cloud_layer_composite_mode_has_background(composite_mode_);
    const bool expects_scene_depth = cloud_layer_composite_mode_has_scene_depth(composite_mode_);
    if (expects_background != background.has_value()) {
        throw std::runtime_error("cloud layer composite background mode mismatch");
    }
    if (expects_scene_depth != scene_depth.has_value()) {
        throw std::runtime_error("cloud layer composite scene depth mode mismatch");
    }
    auto pass = graph.add_pass("cloud composite", RenderGraphQueueDomain::Graphics)
                    .read_texture(frame.product.resolved_cloud)
                    .read_texture(frame.product.resolved_metadata);
    if (background.has_value()) {
        pass.read_texture(background.value());
    }
    if (scene_depth.has_value()) {
        pass.read_texture(scene_depth.value(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    pass.write_color(target)
        .material_pass(cloud_layer_composite_pass_info(background.has_value(),
                                                       scene_depth.has_value()))
        .execute([this, target, frame_slot](const RenderGraphExecutionContext& context) {
            const ColorTargetView color_target = resolved_color_target_view(context, target);
            record_composite_draw(context.recorder(), color_target, frame_slot);
        });
}

void CloudLayerRuntime::update_descriptors(
    const cubey::vulkan::Device& device, FrameSlot frame_slot, const CompiledRenderGraph& graph,
    const RenderGraphResourceSet& resources, const CloudLayerRuntimeFrame& frame,
    std::optional<RenderGraphTextureHandle> background,
    std::optional<RenderGraphTextureHandle> scene_depth) const {
    const bool expects_background = cloud_layer_composite_mode_has_background(composite_mode_);
    const bool expects_scene_depth = cloud_layer_composite_mode_has_scene_depth(composite_mode_);
    if (expects_background != background.has_value()) {
        throw std::runtime_error("cloud layer descriptor background mode mismatch");
    }
    if (expects_scene_depth != scene_depth.has_value()) {
        throw std::runtime_error("cloud layer descriptor scene depth mode mismatch");
    }
    const RenderGraphSampledTextureView cloud_product =
        resolved_sampled_texture_view(graph, resources, frame.product.cloud);
    const RenderGraphSampledTextureView cloud_metadata =
        resolved_sampled_texture_view(graph, resources, frame.product.metadata);
    const RenderGraphSampledTextureView resolved_cloud_product =
        resolved_sampled_texture_view(graph, resources, frame.product.resolved_cloud);
    const RenderGraphSampledTextureView resolved_cloud_metadata =
        resolved_sampled_texture_view(graph, resources, frame.product.resolved_metadata);
    const CloudLayerGeneratedResources& generated = generated_resources();

    MaterialDescriptorWriter(march_material().set(frame_slot))
        .uniform_buffer(kCloudLayerUniformBinding, frame_uniforms().buffer(frame_slot).handle(),
                        frame_uniforms().range())
        .storage_image(kCloudLayerOutputBinding, cloud_product.view, VK_IMAGE_LAYOUT_GENERAL)
        .combined_image_sampler(kCloudLayerBaseNoiseBinding, generated.base_noise.sampler().handle(),
                                generated.base_noise.view())
        .combined_image_sampler(kCloudLayerDetailNoiseBinding,
                                generated.detail_noise.sampler().handle(),
                                generated.detail_noise.view())
        .combined_image_sampler(kCloudLayerWeatherBinding, generated.weather.sampler().handle(),
                                generated.weather.view())
        .storage_image(kCloudLayerMetadataBinding, cloud_metadata.view, VK_IMAGE_LAYOUT_GENERAL)
        .combined_image_sampler(kCloudLayerBlueNoiseBinding,
                                generated.blue_noise.sampler().handle(),
                                generated.blue_noise.view())
        .update(device);

    if (frame.temporal_pass_enabled) {
        const RenderGraphSampledTextureView history_cloud_read =
            resolved_sampled_texture_view(graph, resources, frame.history_cloud_read);
        const RenderGraphSampledTextureView history_metadata_read =
            resolved_sampled_texture_view(graph, resources, frame.history_metadata_read);
        const RenderGraphSampledTextureView history_cloud_write =
            resolved_sampled_texture_view(graph, resources, frame.history_cloud_write);
        const RenderGraphSampledTextureView history_metadata_write =
            resolved_sampled_texture_view(graph, resources, frame.history_metadata_write);
        MaterialDescriptorWriter(temporal_material().set(frame_slot))
            .combined_image_sampler(kCloudLayerTemporalCurrentCloudBinding,
                                    composite_sampler().handle(), cloud_product.view,
                                    cloud_product.layout)
            .combined_image_sampler(kCloudLayerTemporalCurrentMetadataBinding,
                                    composite_sampler().handle(), cloud_metadata.view,
                                    cloud_metadata.layout)
            .combined_image_sampler(kCloudLayerTemporalHistoryCloudBinding,
                                    composite_sampler().handle(), history_cloud_read.view,
                                    history_cloud_read.layout)
            .combined_image_sampler(kCloudLayerTemporalHistoryMetadataBinding,
                                    composite_sampler().handle(), history_metadata_read.view,
                                    history_metadata_read.layout)
            .uniform_buffer(kCloudLayerTemporalUniformBinding,
                            temporal_uniforms().buffer(frame_slot).handle(),
                            temporal_uniforms().range())
            .storage_image(kCloudLayerTemporalOutputBinding, history_cloud_write.view,
                           VK_IMAGE_LAYOUT_GENERAL)
            .storage_image(kCloudLayerTemporalOutputMetadataBinding, history_metadata_write.view,
                           VK_IMAGE_LAYOUT_GENERAL)
            .update(device);
    }

    MaterialDescriptorWriter writer(composite_material().set(frame_slot));
    writer.uniform_buffer(kCloudLayerUniformBinding, frame_uniforms().buffer(frame_slot).handle(),
                          frame_uniforms().range())
        .combined_image_sampler(kCloudLayerCompositeCloudBinding, composite_sampler().handle(),
                                resolved_cloud_product.view, resolved_cloud_product.layout)
        .combined_image_sampler(kCloudLayerCompositeMetadataBinding, composite_sampler().handle(),
                                resolved_cloud_metadata.view, resolved_cloud_metadata.layout);
    if (background.has_value()) {
        const RenderGraphSampledTextureView background_view =
            resolved_sampled_texture_view(graph, resources, background.value());
        writer.combined_image_sampler(kCloudLayerCompositeBackgroundBinding,
                                      composite_sampler().handle(), background_view.view,
                                      background_view.layout);
    }
    if (scene_depth.has_value()) {
        const RenderGraphSampledTextureView scene_depth_view =
            resolved_sampled_texture_view(graph, resources, scene_depth.value());
        writer.combined_image_sampler(kCloudLayerCompositeSceneDepthBinding,
                                      composite_sampler().handle(), scene_depth_view.view,
                                      scene_depth_view.layout);
    }
    writer.update(device);
}

void CloudLayerRuntime::complete_frame(FrameSlot frame_slot, const CloudLayerRuntimeFrame& frame) {
    if (frame.temporal_pass_enabled && frame_slot.index < history_texture_valid_.size() &&
        frame_slot.index < history_read_indices_.size()) {
        history_texture_valid_[frame_slot.index][frame.history_write_index] = true;
        history_read_indices_[frame_slot.index] = frame.history_write_index;
        if (frame_slot.index < history_frame_states_.size()) {
            history_frame_states_[frame_slot.index] = frame.frame_uniforms;
        }
    } else if (!frame.temporal_pass_enabled && frame_slot.index < history_texture_valid_.size()) {
        history_texture_valid_[frame_slot.index] = {false, false};
        if (frame_slot.index < history_frame_states_.size()) {
            history_frame_states_[frame_slot.index].reset();
        }
    }
    ++temporal_frame_index_;
}

const FrameUniformBuffer<CloudLayerFrameUniforms>& CloudLayerRuntime::frame_uniforms() const {
    if (!frame_uniforms_.has_value()) {
        throw std::runtime_error("cloud layer frame uniforms are not initialized");
    }
    return frame_uniforms_.value();
}

const FrameUniformBuffer<CloudLayerTemporalUniforms>& CloudLayerRuntime::temporal_uniforms() const {
    if (!temporal_uniforms_.has_value()) {
        throw std::runtime_error("cloud layer temporal uniforms are not initialized");
    }
    return temporal_uniforms_.value();
}

const MaterialInstance& CloudLayerRuntime::march_material() const {
    if (!march_material_.has_value()) {
        throw std::runtime_error("cloud layer march material is not initialized");
    }
    return march_material_.value();
}

const MaterialInstance& CloudLayerRuntime::temporal_material() const {
    if (!temporal_material_.has_value()) {
        throw std::runtime_error("cloud layer temporal material is not initialized");
    }
    return temporal_material_.value();
}

const MaterialInstance& CloudLayerRuntime::composite_material() const {
    if (!composite_material_.has_value()) {
        throw std::runtime_error("cloud layer composite material is not initialized");
    }
    return composite_material_.value();
}

const ComputePipelineResource& CloudLayerRuntime::march_pipeline() const {
    if (!march_pipeline_.has_value()) {
        throw std::runtime_error("cloud layer march pipeline is not initialized");
    }
    return march_pipeline_.value();
}

const ComputePipelineResource& CloudLayerRuntime::temporal_pipeline() const {
    if (!temporal_pipeline_.has_value()) {
        throw std::runtime_error("cloud layer temporal pipeline is not initialized");
    }
    return temporal_pipeline_.value();
}

const GraphicsPipelineResource& CloudLayerRuntime::composite_pipeline() const {
    if (!composite_pipeline_.has_value()) {
        throw std::runtime_error("cloud layer composite pipeline is not initialized");
    }
    return composite_pipeline_.value();
}

const cubey::vulkan::Sampler& CloudLayerRuntime::composite_sampler() const {
    if (!composite_sampler_.has_value()) {
        throw std::runtime_error("cloud layer composite sampler is not initialized");
    }
    return composite_sampler_.value();
}

const Texture2D& CloudLayerRuntime::history_cloud_texture(FrameSlot frame_slot,
                                                         std::uint32_t ping_pong) const {
    if (frame_slot.index >= history_cloud_textures_.size() || ping_pong >= 2U ||
        !history_cloud_textures_[frame_slot.index][ping_pong].has_value()) {
        throw std::runtime_error("cloud layer history texture is not initialized");
    }
    return history_cloud_textures_[frame_slot.index][ping_pong].value();
}

const Texture2D& CloudLayerRuntime::history_metadata_texture(FrameSlot frame_slot,
                                                            std::uint32_t ping_pong) const {
    if (frame_slot.index >= history_metadata_textures_.size() || ping_pong >= 2U ||
        !history_metadata_textures_[frame_slot.index][ping_pong].has_value()) {
        throw std::runtime_error("cloud layer metadata history texture is not initialized");
    }
    return history_metadata_textures_[frame_slot.index][ping_pong].value();
}

bool CloudLayerRuntime::history_texture_valid(FrameSlot frame_slot,
                                              std::uint32_t ping_pong) const {
    return frame_slot.index < history_texture_valid_.size() && ping_pong < 2U &&
           history_texture_valid_[frame_slot.index][ping_pong];
}

bool CloudLayerRuntime::history_extent_matches(VkExtent2D extent) const noexcept {
    if (history_cloud_textures_.empty() || history_cloud_textures_.front().front() == std::nullopt) {
        return false;
    }
    return cloud_layer_extent_equal(history_cloud_textures_.front().front()->extent(), extent);
}

CloudLayerTemporalUniforms CloudLayerRuntime::temporal_frame_uniforms(
    FrameSlot frame_slot, CloudLayerFrameUniforms current,
    std::uint32_t history_read_index) const {
    const bool has_frame_state = frame_slot.index < history_frame_states_.size() &&
                                 history_frame_states_[frame_slot.index].has_value();
    const bool reset =
        !history_texture_valid(frame_slot, history_read_index) || !has_frame_state ||
        !cloud_layer_history_uniforms_compatible(
            has_frame_state ? history_frame_states_[frame_slot.index].value() : current, current);
    const CloudLayerFrameUniforms previous =
        reset ? current : history_frame_states_[frame_slot.index].value();
    const float current_weight = reset ? 1.0F : current.temporal_options.z;
    return {
        .current_camera_right_aspect = current.camera_right_aspect,
        .current_camera_up_tan_half_fovy = current.camera_up_tan_half_fovy,
        .current_camera_forward_mode = current.camera_forward_mode,
        .current_camera_position_radius = current.camera_position_radius,
        .previous_camera_right_aspect = previous.camera_right_aspect,
        .previous_camera_up_tan_half_fovy = previous.camera_up_tan_half_fovy,
        .previous_camera_forward_mode = previous.camera_forward_mode,
        .previous_camera_position_radius = previous.camera_position_radius,
        .current_weather = current.weather,
        .previous_weather = previous.weather,
        .options =
            {
                current_weight,
                reset ? 1.0F : 0.0F,
                current.temporal_options.y,
                current.temporal_options.w,
            },
    };
}

void CloudLayerRuntime::create_history_textures(const cubey::vulkan::Device& device,
                                                VkExtent2D extent,
                                                std::uint32_t frame_slot_count) {
    history_cloud_textures_.clear();
    history_metadata_textures_.clear();
    history_read_indices_.assign(frame_slot_count, 0U);
    history_texture_valid_.assign(frame_slot_count, {false, false});
    history_frame_states_.assign(frame_slot_count, std::nullopt);
    history_cloud_textures_.resize(frame_slot_count);
    history_metadata_textures_.resize(frame_slot_count);
    for (std::array<std::optional<Texture2D>, 2>& textures : history_cloud_textures_) {
        for (std::optional<Texture2D>& texture : textures) {
            texture.emplace(device, cloud_layer_history_texture_config(extent));
        }
    }
    for (std::array<std::optional<Texture2D>, 2>& textures : history_metadata_textures_) {
        for (std::optional<Texture2D>& texture : textures) {
            texture.emplace(device, cloud_layer_history_texture_config(extent));
        }
    }
}

void CloudLayerRuntime::invalidate_history() {
    for (std::array<bool, 2>& valid : history_texture_valid_) {
        valid = {false, false};
    }
    std::fill(history_read_indices_.begin(), history_read_indices_.end(), 0U);
    for (std::optional<CloudLayerFrameUniforms>& frame_state : history_frame_states_) {
        frame_state.reset();
    }
}

void CloudLayerRuntime::record_march_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                             VkDescriptorSet descriptor_set,
                                             VkExtent2D extent) const {
    record_compute_pipeline_dispatch(
        recorder,
        compute_pipeline_dispatch_info(
            march_pipeline(), descriptor_set,
            ceil_dispatch_groups(extent.width, extent.height, kCloudLayerComputeGroupSize)));
}

void CloudLayerRuntime::record_temporal_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                                                VkDescriptorSet descriptor_set,
                                                VkExtent2D extent) const {
    record_compute_pipeline_dispatch(
        recorder,
        compute_pipeline_dispatch_info(
            temporal_pipeline(), descriptor_set,
            ceil_dispatch_groups(extent.width, extent.height, kCloudLayerComputeGroupSize)));
}

void CloudLayerRuntime::record_composite_draw(const cubey::vulkan::CommandRecorder& recorder,
                                             const ColorTargetView& target,
                                             FrameSlot frame_slot) const {
    record_render_target_pass(
        recorder, render_target_view(target),
        RenderClearValues{
            .color = color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        [this, frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
            record_fullscreen_pipeline_draw(
                pass_recorder,
                {.pipeline = &composite_pipeline(),
                 .descriptor_set = composite_material().set(frame_slot)});
        });
}

} // namespace cubey::render
