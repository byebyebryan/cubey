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

#include "cloud_layer_internal.h"

namespace cubey::render {

using detail::CloudLayerShadowPushConstants;
using detail::cloud_layer_composite_mode_has_background;
using detail::cloud_layer_composite_mode_has_scene_depth;

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

[[nodiscard]] MaterialDescriptorSetLayout cloud_layer_shadow_set_layout() {
    constexpr VkShaderStageFlags compute_stage = VK_SHADER_STAGE_COMPUTE_BIT;
    return {
        .set = 0,
        .bindings =
            {
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerShadowUniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = compute_stage,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerShadowOutputBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = compute_stage,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerShadowBaseNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = compute_stage,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerShadowDetailNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = compute_stage,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudLayerShadowWeatherBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = compute_stage,
                },
            },
    };
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


} // namespace


CloudLayerRuntimeShaderFiles
cloud_layer_runtime_shader_files(const std::filesystem::path& shader_dir,
                                 CloudLayerCompositeMode composite_mode) {
    auto shader_path = [&shader_dir](const char* filename) { return shader_dir / filename; };
    const char* composite_fragment = "cloud_composite_background.frag.spv";
    if (composite_mode == CloudLayerCompositeMode::ExternalBackgroundSceneDepth) {
        composite_fragment = "cloud_composite_background_depth.frag.spv";
    }
    return {
        .generated =
            {
                .base_noise =
                    compute_shader_file(shader_path("cloud_perlin_worley.comp.spv")),
                .detail_noise = compute_shader_file(shader_path("cloud_worley.comp.spv")),
                .weather = compute_shader_file(shader_path("cloud_weather.comp.spv")),
                .blue_noise = compute_shader_file(shader_path("cloud_blue_noise.comp.spv")),
            },
        .general_march = compute_shader_file(shader_path("cloud_march.comp.spv")),
        .surface_march = compute_shader_file(shader_path("surface_cloud_march.comp.spv")),
        .shadow = compute_shader_file(shader_path("cloud_shadow.comp.spv")),
        .temporal = compute_shader_file(shader_path("cloud_temporal.comp.spv")),
        .composite_vertex = vertex_shader_file(shader_path("cloud.vert.spv")),
        .composite_fragment = fragment_shader_file(shader_path(composite_fragment)),
    };
}

MaterialPassInfo cloud_layer_march_pass_info() {
    return {
        .label = "cloud_march",
        .descriptor_sets = {cloud_layer_march_set_layout()},
    };
}

MaterialPassInfo cloud_layer_shadow_pass_info() {
    return {
        .label = "cloud_shadow",
        .descriptor_sets = {cloud_layer_shadow_set_layout()},
        .push_constants = {{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(CloudLayerShadowPushConstants),
        }},
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

void CloudLayerRuntime::destroy_swapchain_resources() {
    shadow_sampler_.reset();
    composite_sampler_.reset();
    temporal_material_.reset();
    shadow_material_.reset();
    march_material_.reset();
    composite_material_.reset();
    temporal_pipeline_.reset();
    general_march_pipeline_.reset();
    surface_march_pipeline_.reset();
    shadow_pipeline_.reset();
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
    general_march_pipeline_.emplace(device, ComputePipelineResourceConfig{
                                                .shader_stage = shaders.general_march,
                                                .descriptor_set_layouts = march_layouts,
                                            });
    surface_march_pipeline_.emplace(device, ComputePipelineResourceConfig{
                                                .shader_stage = shaders.surface_march,
                                                .descriptor_set_layouts = march_layouts,
                                            });

    const MaterialPassInfo shadow_pass = cloud_layer_shadow_pass_info();
    shadow_material_.emplace(device, MaterialInstanceConfig{
                                         .material_pass = shadow_pass,
                                         .descriptor_set = 0,
                                         .set_count = frame_slot_count,
                                     });
    const std::array<VkDescriptorSetLayout, 1> shadow_layouts{shadow_material_->layout()};
    const std::array<VkPushConstantRange, 1> shadow_push_constants{
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(CloudLayerShadowPushConstants),
        },
    };
    shadow_pipeline_.emplace(device, ComputePipelineResourceConfig{
                                         .shader_stage = shaders.shadow,
                                         .descriptor_set_layouts = shadow_layouts,
                                         .push_constants = shadow_push_constants,
                                     });
    shadow_sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                        .min_filter = VK_FILTER_LINEAR,
                                        .mag_filter = VK_FILTER_LINEAR,
                                        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                        .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
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

const cubey::vulkan::Sampler& CloudLayerRuntime::product_sampler() const {
    return composite_sampler();
}

const cubey::vulkan::Sampler& CloudLayerRuntime::shadow_sampler() const {
    if (!shadow_sampler_.has_value()) {
        throw std::runtime_error("cloud layer shadow sampler is not initialized");
    }
    return shadow_sampler_.value();
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

const MaterialInstance& CloudLayerRuntime::shadow_material() const {
    if (!shadow_material_.has_value()) {
        throw std::runtime_error("cloud layer shadow material is not initialized");
    }
    return shadow_material_.value();
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

const ComputePipelineResource& CloudLayerRuntime::general_march_pipeline() const {
    if (!general_march_pipeline_.has_value()) {
        throw std::runtime_error("cloud layer general march pipeline is not initialized");
    }
    return general_march_pipeline_.value();
}

const ComputePipelineResource& CloudLayerRuntime::surface_march_pipeline() const {
    if (!surface_march_pipeline_.has_value()) {
        throw std::runtime_error("cloud layer surface march pipeline is not initialized");
    }
    return surface_march_pipeline_.value();
}

const ComputePipelineResource& CloudLayerRuntime::shadow_pipeline() const {
    if (!shadow_pipeline_.has_value()) {
        throw std::runtime_error("cloud layer shadow pipeline is not initialized");
    }
    return shadow_pipeline_.value();
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


} // namespace cubey::render
