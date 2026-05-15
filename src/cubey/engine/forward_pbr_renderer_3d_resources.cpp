#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_internal.h"

#include <cubey/render/pass.h>

#include <array>
#include <span>
#include <stdexcept>

namespace cubey {
namespace {

void validate_scene_color_format(const vulkan::Device& device, VkFormat format) {
    if (format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("forward PBR renderer requires a scene color format");
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device.physical_device(), format, &properties);
    constexpr VkFormatFeatureFlags kRequired =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & kRequired) != kRequired) {
        throw std::runtime_error(
            "forward PBR scene color format does not support color attachment sampling");
    }
}

} // namespace

void ForwardPbrRenderer3D::create_global_resources(
    const vulkan::Device& device, const render::GeneratedPbrEnvironment& environment,
    std::uint32_t frame_slot_count) {
    impl_->create_global_resources(device, environment, frame_slot_count);
}

void ForwardPbrRenderer3D::Impl::create_global_resources(
    const vulkan::Device& device, const render::GeneratedPbrEnvironment& environment,
    std::uint32_t frame_slot_count) {
    if (frame_slot_count == 0) {
        throw std::runtime_error("forward PBR renderer requires at least one frame slot");
    }
    global_.environment = &environment;
    global_.graph_executor.resize(frame_slot_count);

    const VkPushConstantRange shadow_push_constants{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ForwardPbrRenderer3DShadowPushConstants),
    };
    const std::array<render::ShaderStageFile, 1> shadow_shaders{
        render::vertex_shader_file(config_.shadow_depth_vertex_shader),
    };
    const render::VertexInputLayout shadow_vertex_input =
        forward_pbr_renderer_3d_shadow_vertex_input_layout();
    global_.shadow_pass.emplace(
        device, render::ShadowMapPass3DConfig{
                    .extent = {config_.shadow_extent, config_.shadow_extent},
                    .depth_format = config_.shadow_depth_format,
                    .pipeline =
                        {
                            .shader_stage_files = shadow_shaders,
                            .vertex_bindings = shadow_vertex_input.bindings(),
                            .vertex_attributes = shadow_vertex_input.attribute_descriptions(),
                            .material_pass = render::shadow_depth_pass_info({
                                .label = "forward_pbr.shadow",
                                .push_constants =
                                    std::span<const VkPushConstantRange>{&shadow_push_constants, 1},
                                .cull_mode = VK_CULL_MODE_BACK_BIT,
                            }),
                        },
                });
    pipeline_variant_slot(ForwardPbrPipelineVariant::ShadowDoubleSided)
        .emplace(device,
                 render::graphics_pipeline_file_resource_config(
                     {
                         .extent = shadow_pass().depth_target().extent,
                         .depth_format = shadow_pass().depth_target().format,
                     },
                     {
                         .shader_stage_files = shadow_shaders,
                         .vertex_bindings = shadow_vertex_input.bindings(),
                         .vertex_attributes = shadow_vertex_input.attribute_descriptions(),
                         .material_pass = render::shadow_depth_pass_info({
                             .label = "forward_pbr.shadow.double_sided",
                             .push_constants =
                                 std::span<const VkPushConstantRange>{&shadow_push_constants, 1},
                             .cull_mode = VK_CULL_MODE_NONE,
                         }),
                     }));

    global_.skybox_material.emplace(
        device, render::FrameUniformMaterialInstanceConfig{
                    .material_pass = render::pbr_skybox_pass_info(),
                    .descriptor_set = 0,
                    .frame_slot_count = frame_slot_count,
                    .uniform_binding =
                        forward_pbr_renderer_3d_binding(render::PbrSkyboxBinding::SkyboxUniforms),
                    .sampled_images =
                        {
                            render::SampledImageMaterialBinding{
                                .binding = forward_pbr_renderer_3d_binding(
                                    render::PbrSkyboxBinding::EnvironmentCube),
                                .sampler = environment.prefiltered_cube.sampler().handle(),
                                .image_view = environment.prefiltered_cube.view(),
                            },
                        },
                });

    const render::DepthTexture& shadow_texture = shadow_pass().depth_texture();
    global_.scene_material.emplace(
        device,
        render::FrameUniformMaterialInstanceConfig{
            .material_pass = render::pbr_forward_pass_info(),
            .descriptor_set = 0,
            .frame_slot_count = frame_slot_count,
            .uniform_binding =
                forward_pbr_renderer_3d_binding(render::PbrSceneBinding::SceneUniforms),
            .sampled_images =
                {
                    render::SampledImageMaterialBinding{
                        .binding =
                            forward_pbr_renderer_3d_binding(render::PbrSceneBinding::ShadowMap),
                        .sampler = shadow_texture.sampler().handle(),
                        .image_view = shadow_texture.view(),
                        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    },
                    render::SampledImageMaterialBinding{
                        .binding = forward_pbr_renderer_3d_binding(
                            render::PbrSceneBinding::IrradianceCube),
                        .sampler = environment.irradiance_cube.sampler().handle(),
                        .image_view = environment.irradiance_cube.view(),
                    },
                    render::SampledImageMaterialBinding{
                        .binding = forward_pbr_renderer_3d_binding(
                            render::PbrSceneBinding::PrefilteredCube),
                        .sampler = environment.prefiltered_cube.sampler().handle(),
                        .image_view = environment.prefiltered_cube.view(),
                    },
                    render::SampledImageMaterialBinding{
                        .binding =
                            forward_pbr_renderer_3d_binding(render::PbrSceneBinding::BrdfLut),
                        .sampler = environment.brdf_lut.sampler().handle(),
                        .image_view = environment.brdf_lut.view(),
                    },
                },
        });
    global_.post_material.emplace(device, render::FrameUniformMaterialInstanceConfig{
                                       .material_pass = render::pbr_post_pass_info(),
                                       .descriptor_set = 0,
                                       .frame_slot_count = frame_slot_count,
                                       .uniform_binding = forward_pbr_renderer_3d_binding(
                                           render::PbrPostBinding::PostUniforms),
                                   });
}

void ForwardPbrRenderer3D::create_swapchain_resources(
    const vulkan::Device& device, const ForwardPbrRenderer3DTargetResourcesInfo& info) {
    impl_->create_swapchain_resources(device, info);
}

void ForwardPbrRenderer3D::Impl::create_swapchain_resources(
    const vulkan::Device& device, const ForwardPbrRenderer3DTargetResourcesInfo& info) {
    if (info.extent.width == 0 || info.extent.height == 0) {
        throw std::runtime_error("forward PBR renderer requires a nonzero target extent");
    }
    if (info.color_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("forward PBR renderer requires a color format");
    }
    if (info.materials == nullptr) {
        throw std::runtime_error("forward PBR renderer requires a PBR material table");
    }
    validate_scene_color_format(device, config_.scene_color_format);

    swapchain_.depth_attachment.emplace(device, info.extent);
    swapchain_.post_sampler.emplace(device, vulkan::SamplerConfig{
                                      .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                  });

    const std::array<render::ShaderStageFile, 2> skybox_shaders{
        render::vertex_shader_file(config_.skybox_vertex_shader),
        render::fragment_shader_file(config_.skybox_fragment_shader),
    };
    const std::array<VkDescriptorSetLayout, 1> skybox_layouts{skybox_material().layout()};
    swapchain_.skybox_pipeline.emplace(device, render::graphics_pipeline_file_resource_config(
                                         {
                                             .extent = info.extent,
                                             .color_format = config_.scene_color_format,
                                             .depth_format = depth_attachment().format(),
                                         },
                                         {
                                             .shader_stage_files = skybox_shaders,
                                             .descriptor_set_layouts = skybox_layouts,
                                             .material_pass = render::pbr_skybox_pass_info(),
                                         }));

    const render::VertexInputLayout vertex_input = render::pbr_vertex_input_layout();
    const std::array<render::ShaderStageFile, 2> pbr_shaders{
        render::vertex_shader_file(config_.pbr_vertex_shader),
        render::fragment_shader_file(config_.pbr_fragment_shader),
    };
    const std::array<VkDescriptorSetLayout, 2> pbr_layouts{
        scene_material().layout(),
        info.materials->descriptor_set_layout(),
    };

    const auto create_pbr_pipeline = [&](ForwardPbrPipelineVariant variant, const char* label,
                                         render::MaterialBlendMode blend,
                                         VkCullModeFlags cull_mode) {
        pipeline_variant_slot(variant).emplace(
            device,
            render::graphics_pipeline_file_resource_config(
                {
                    .extent = info.extent,
                    .color_format = config_.scene_color_format,
                    .depth_format = depth_attachment().format(),
                },
                {
                    .shader_stage_files = pbr_shaders,
                    .vertex_bindings = vertex_input.bindings(),
                    .vertex_attributes = vertex_input.attribute_descriptions(),
                    .descriptor_set_layouts = pbr_layouts,
                    .material_pass = render::pbr_forward_pass_info(render::PbrForwardPassConfig{
                        .blend = blend,
                        .cull_mode = cull_mode,
                        .label = label,
                    }),
                }));
    };
    create_pbr_pipeline(ForwardPbrPipelineVariant::Opaque, "forward_pbr.forward.opaque",
                        render::MaterialBlendMode::Opaque, VK_CULL_MODE_BACK_BIT);
    create_pbr_pipeline(ForwardPbrPipelineVariant::OpaqueDoubleSided,
                        "forward_pbr.forward.opaque.double_sided",
                        render::MaterialBlendMode::Opaque, VK_CULL_MODE_NONE);
    create_pbr_pipeline(ForwardPbrPipelineVariant::Alpha, "forward_pbr.forward.alpha",
                        render::MaterialBlendMode::AlphaBlend, VK_CULL_MODE_BACK_BIT);
    create_pbr_pipeline(ForwardPbrPipelineVariant::AlphaDoubleSided,
                        "forward_pbr.forward.alpha.double_sided",
                        render::MaterialBlendMode::AlphaBlend, VK_CULL_MODE_NONE);

    const VkPushConstantRange shadow_push_constants{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ForwardPbrRenderer3DShadowPushConstants),
    };
    const std::array<render::ShaderStageFile, 2> mask_shadow_shaders{
        render::vertex_shader_file(config_.shadow_depth_vertex_shader),
        render::fragment_shader_file(config_.shadow_depth_fragment_shader),
    };
    const std::array<VkDescriptorSetLayout, 2> mask_shadow_layouts{
        scene_material().layout(),
        info.materials->descriptor_set_layout(),
    };
    const render::VertexInputLayout shadow_vertex_input =
        forward_pbr_renderer_3d_shadow_vertex_input_layout();
    pipeline_variant_slot(ForwardPbrPipelineVariant::MaskShadow)
        .emplace(device,
                 render::graphics_pipeline_file_resource_config(
                     {
                         .extent = shadow_pass().depth_target().extent,
                         .depth_format = shadow_pass().depth_target().format,
                     },
                     {
                         .shader_stage_files = mask_shadow_shaders,
                         .vertex_bindings = shadow_vertex_input.bindings(),
                         .vertex_attributes = shadow_vertex_input.attribute_descriptions(),
                         .descriptor_set_layouts = mask_shadow_layouts,
                         .material_pass = render::shadow_depth_pass_info({
                             .label = "forward_pbr.shadow.mask",
                             .push_constants =
                                 std::span<const VkPushConstantRange>{&shadow_push_constants, 1},
                             .cull_mode = VK_CULL_MODE_BACK_BIT,
                         }),
                     }));
    pipeline_variant_slot(ForwardPbrPipelineVariant::MaskShadowDoubleSided)
        .emplace(device,
                 render::graphics_pipeline_file_resource_config(
                     {
                         .extent = shadow_pass().depth_target().extent,
                         .depth_format = shadow_pass().depth_target().format,
                     },
                     {
                         .shader_stage_files = mask_shadow_shaders,
                         .vertex_bindings = shadow_vertex_input.bindings(),
                         .vertex_attributes = shadow_vertex_input.attribute_descriptions(),
                         .descriptor_set_layouts = mask_shadow_layouts,
                         .material_pass = render::shadow_depth_pass_info({
                             .label = "forward_pbr.shadow.mask.double_sided",
                             .push_constants =
                                 std::span<const VkPushConstantRange>{&shadow_push_constants, 1},
                             .cull_mode = VK_CULL_MODE_NONE,
                         }),
                     }));

    const std::array<render::ShaderStageFile, 2> post_shaders{
        render::vertex_shader_file(config_.post_vertex_shader),
        render::fragment_shader_file(config_.post_fragment_shader),
    };
    const std::array<VkDescriptorSetLayout, 1> post_layouts{post_material().layout()};
    swapchain_.post_pipeline.emplace(device, render::graphics_pipeline_file_resource_config(
                                       {
                                           .extent = info.extent,
                                           .color_format = info.color_format,
                                       },
                                       {
                                           .shader_stage_files = post_shaders,
                                           .descriptor_set_layouts = post_layouts,
                                           .material_pass = render::pbr_post_pass_info(),
                                       }));
}

void ForwardPbrRenderer3D::destroy_swapchain_resources() {
    impl_->destroy_swapchain_resources();
}

void ForwardPbrRenderer3D::Impl::destroy_swapchain_resources() {
    const std::uint32_t frame_slot_count = global_.graph_executor.frame_slot_count();
    global_.graph_executor.clear();
    if (frame_slot_count != 0) {
        global_.graph_executor.resize(frame_slot_count);
    }
    swapchain_.post_pipeline.reset();
    swapchain_.post_sampler.reset();
    pipeline_variant_slot(ForwardPbrPipelineVariant::MaskShadowDoubleSided).reset();
    pipeline_variant_slot(ForwardPbrPipelineVariant::MaskShadow).reset();
    pipeline_variant_slot(ForwardPbrPipelineVariant::AlphaDoubleSided).reset();
    pipeline_variant_slot(ForwardPbrPipelineVariant::Alpha).reset();
    pipeline_variant_slot(ForwardPbrPipelineVariant::OpaqueDoubleSided).reset();
    pipeline_variant_slot(ForwardPbrPipelineVariant::Opaque).reset();
    swapchain_.skybox_pipeline.reset();
    swapchain_.depth_attachment.reset();
}

void ForwardPbrRenderer3D::destroy_all_resources() {
    impl_->destroy_all_resources();
}

void ForwardPbrRenderer3D::Impl::destroy_all_resources() {
    destroy_swapchain_resources();
    global_.graph_executor.clear();
    global_.post_material.reset();
    global_.scene_material.reset();
    global_.skybox_material.reset();
    for (std::optional<render::GraphicsPipelineResource>& pipeline : swapchain_.pipeline_variants) {
        pipeline.reset();
    }
    global_.shadow_double_sided_pipeline.reset();
    global_.shadow_pass.reset();
    global_.environment = nullptr;
    swapchain_.shadow_depth_is_sampled = false;
}

} // namespace cubey
