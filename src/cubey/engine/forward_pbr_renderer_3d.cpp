#include <cubey/engine/forward_pbr_renderer_3d.h>

#include <cubey/render/pass.h>
#include <cubey/scene/render_recording.h>

#include <glm/matrix.hpp>

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace cubey {
namespace {

constexpr float kDegreesToRadians = 0.017453292519943295769F;

struct ShadowPushConstants {
    math::Mat4 light_mvp{1.0F};
};

static_assert(sizeof(ShadowPushConstants) == sizeof(math::Mat4));

[[nodiscard]] render::RenderGraphTextureState undefined_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

[[nodiscard]] render::RenderGraphTextureState sampled_depth_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
}

[[nodiscard]] math::Vec3 camera_world_position(const SceneReadView& view, Entity camera) {
    const TransformInstance3D instance = view.transforms3d().instance(camera);
    const math::Mat4& world = view.transforms3d().world_affine_matrix(instance);
    return {world[3].x, world[3].y, world[3].z};
}

[[nodiscard]] float rotation_radians(float degrees) {
    return degrees * kDegreesToRadians;
}

[[nodiscard]] std::uint32_t binding(render::PbrSceneBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t binding(render::PbrSkyboxBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t binding(render::PbrPostBinding value) {
    return static_cast<std::uint32_t>(value);
}

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

void validate_forward_pbr_renderer_3d_config(const ForwardPbrRenderer3DConfig& config) {
    if (config.pbr_vertex_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a PBR vertex shader");
    }
    if (config.pbr_fragment_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a PBR fragment shader");
    }
    if (config.skybox_vertex_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a skybox vertex shader");
    }
    if (config.skybox_fragment_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a skybox fragment shader");
    }
    if (config.post_vertex_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a post vertex shader");
    }
    if (config.post_fragment_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a post fragment shader");
    }
    if (config.shadow_depth_vertex_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a shadow depth vertex shader");
    }
    if (config.shadow_extent == 0) {
        throw std::runtime_error("forward PBR renderer requires a nonzero shadow extent");
    }
    if (config.scene_color_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("forward PBR renderer requires a scene color format");
    }
}

void validate_forward_pbr_renderer_3d_render_request(
    const ForwardPbrRenderer3DRenderRequest& request) {
    if (request.target.device == nullptr ||
        request.target.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("forward PBR render request requires device and command buffer");
    }
    if (request.view.scene == nullptr || request.view.shadow_plan == nullptr ||
        request.view.scene_plan == nullptr) {
        throw std::runtime_error("forward PBR render request requires scene and frame plans");
    }
    if (request.resources.meshes == nullptr || request.resources.material_instances == nullptr ||
        request.resources.material_factors == nullptr) {
        throw std::runtime_error("forward PBR render request requires render resource tables");
    }
}

LightPacket3D forward_pbr_renderer_3d_selected_light(std::span<const LightPacket3D> lights,
                                                      Entity requested_light,
                                                      LightPacket3D fallback_light) {
    for (const LightPacket3D& light : lights) {
        if (light.entity == requested_light) {
            return light;
        }
    }
    return fallback_light;
}

render::VertexInputLayout forward_pbr_renderer_3d_shadow_vertex_input_layout() {
    return render::vertex_position_only_input_layout(sizeof(render::PbrVertex));
}

render::PbrSceneUniforms forward_pbr_renderer_3d_scene_uniforms(const ForwardPbrRenderer3DSceneUniformInfo& info) {
    const math::Vec3 ambient =
        info.environment.ambient_color * info.environment.ambient_intensity;
    const float radians = rotation_radians(info.environment_rotation_degrees);
    return {
        .view_projection = info.view_projection,
        .light_view_projection = info.light_view_projection,
        .camera_position = {info.camera_position, 1.0F},
        .light_direction = {info.light.direction, 0.0F},
        .light_color_intensity = {info.light.color, info.light.intensity},
        .ambient_color_intensity = {ambient, 1.0F},
        .environment_intensity_mip_count =
            {
                info.environment_intensity,
                static_cast<float>(info.prefiltered_mip_levels),
                std::cos(radians),
                std::sin(radians),
            },
    };
}

render::PbrSkyboxUniforms forward_pbr_renderer_3d_skybox_uniforms(const ForwardPbrRenderer3DSkyboxUniformInfo& info) {
    const float radians = rotation_radians(info.environment_rotation_degrees);
    return {
        .inverse_view_projection = glm::inverse(info.view_projection),
        .camera_position = {info.camera_position, 1.0F},
        .environment_rotation_intensity =
            {
                std::cos(radians),
                std::sin(radians),
                info.environment_intensity,
                0.0F,
            },
    };
}

render::PbrPostUniforms
forward_pbr_renderer_3d_post_uniforms(const ForwardPbrRenderer3DPostUniformInfo& info) {
    const render::PbrDisplayTransform display_transform =
        render::pbr_display_transform_for_target(info.color_format, info.exposure, info.tonemap);
    return {
        .display_transform = render::pbr_display_transform_uniform(display_transform),
    };
}

ForwardPbrRenderer3D::ForwardPbrRenderer3D(ForwardPbrRenderer3DConfig config)
    : config_(std::move(config)) {
    validate_forward_pbr_renderer_3d_config(config_);
}

void ForwardPbrRenderer3D::create_global_resources(
    const vulkan::Device& device, const render::GeneratedPbrEnvironment& environment,
    std::uint32_t frame_slot_count) {
    if (frame_slot_count == 0) {
        throw std::runtime_error("forward PBR renderer requires at least one frame slot");
    }
    environment_ = &environment;
    graph_executor_.resize(frame_slot_count);

    const VkPushConstantRange shadow_push_constants{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ShadowPushConstants),
    };
    const std::array<render::ShaderStageFile, 1> shadow_shaders{
        render::vertex_shader_file(config_.shadow_depth_vertex_shader),
    };
    const render::VertexInputLayout shadow_vertex_input = forward_pbr_renderer_3d_shadow_vertex_input_layout();
    shadow_pass_.emplace(
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
                                    std::span<const VkPushConstantRange>{&shadow_push_constants,
                                                                         1},
                            }),
                        },
                });

    skybox_material_.emplace(
        device,
        render::FrameUniformMaterialInstanceConfig{
            .material_pass = render::pbr_skybox_pass_info(),
            .descriptor_set = 0,
            .frame_slot_count = frame_slot_count,
            .uniform_binding = binding(render::PbrSkyboxBinding::SkyboxUniforms),
            .sampled_images =
                {
                    render::SampledImageMaterialBinding{
                        .binding = binding(render::PbrSkyboxBinding::EnvironmentCube),
                        .sampler = environment.prefiltered_cube.sampler().handle(),
                        .image_view = environment.prefiltered_cube.view(),
                    },
                },
        });

    const render::DepthTexture& shadow_texture = shadow_pass().depth_texture();
    scene_material_.emplace(
        device,
        render::FrameUniformMaterialInstanceConfig{
            .material_pass = render::pbr_forward_pass_info(),
            .descriptor_set = 0,
            .frame_slot_count = frame_slot_count,
            .uniform_binding = binding(render::PbrSceneBinding::SceneUniforms),
            .sampled_images =
                {
                    render::SampledImageMaterialBinding{
                        .binding = binding(render::PbrSceneBinding::ShadowMap),
                        .sampler = shadow_texture.sampler().handle(),
                        .image_view = shadow_texture.view(),
                        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    },
                    render::SampledImageMaterialBinding{
                        .binding = binding(render::PbrSceneBinding::IrradianceCube),
                        .sampler = environment.irradiance_cube.sampler().handle(),
                        .image_view = environment.irradiance_cube.view(),
                    },
                    render::SampledImageMaterialBinding{
                        .binding = binding(render::PbrSceneBinding::PrefilteredCube),
                        .sampler = environment.prefiltered_cube.sampler().handle(),
                        .image_view = environment.prefiltered_cube.view(),
                    },
                    render::SampledImageMaterialBinding{
                        .binding = binding(render::PbrSceneBinding::BrdfLut),
                        .sampler = environment.brdf_lut.sampler().handle(),
                        .image_view = environment.brdf_lut.view(),
                    },
                },
        });
    post_material_.emplace(
        device,
        render::FrameUniformMaterialInstanceConfig{
            .material_pass = render::pbr_post_pass_info(),
            .descriptor_set = 0,
            .frame_slot_count = frame_slot_count,
            .uniform_binding = binding(render::PbrPostBinding::PostUniforms),
        });
}

void ForwardPbrRenderer3D::create_swapchain_resources(
    const vulkan::Device& device, const ForwardPbrRenderer3DTargetResourcesInfo& info) {
    if (info.extent.width == 0 || info.extent.height == 0) {
        throw std::runtime_error("forward PBR renderer requires a nonzero target extent");
    }
    if (info.color_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("forward PBR renderer requires a color format");
    }
    if (info.material_descriptor_set_layout == VK_NULL_HANDLE) {
        throw std::runtime_error("forward PBR renderer requires a material descriptor set layout");
    }
    validate_scene_color_format(device, config_.scene_color_format);

    depth_attachment_.emplace(device, info.extent);
    post_sampler_.emplace(device, vulkan::SamplerConfig{
                                      .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                  });

    const std::array<render::ShaderStageFile, 2> skybox_shaders{
        render::vertex_shader_file(config_.skybox_vertex_shader),
        render::fragment_shader_file(config_.skybox_fragment_shader),
    };
    const std::array<VkDescriptorSetLayout, 1> skybox_layouts{skybox_material().layout()};
    skybox_pipeline_.emplace(
        device,
        render::graphics_pipeline_file_resource_config(
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
        info.material_descriptor_set_layout,
    };

    opaque_pipeline_.emplace(
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
                .material_pass =
                    render::pbr_forward_pass_info(render::PbrForwardPassConfig{
                        .label = "forward_pbr.forward.opaque",
                    }),
            }));
    alpha_pipeline_.emplace(
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
                .material_pass =
                    render::pbr_forward_pass_info(render::PbrForwardPassConfig{
                        .blend = render::MaterialBlendMode::AlphaBlend,
                        .label = "forward_pbr.forward.alpha",
                    }),
            }));

    const std::array<render::ShaderStageFile, 2> post_shaders{
        render::vertex_shader_file(config_.post_vertex_shader),
        render::fragment_shader_file(config_.post_fragment_shader),
    };
    const std::array<VkDescriptorSetLayout, 1> post_layouts{post_material().layout()};
    post_pipeline_.emplace(
        device,
        render::graphics_pipeline_file_resource_config(
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
    const std::uint32_t frame_slot_count = graph_executor_.frame_slot_count();
    graph_executor_.clear();
    if (frame_slot_count != 0) {
        graph_executor_.resize(frame_slot_count);
    }
    post_pipeline_.reset();
    post_sampler_.reset();
    alpha_pipeline_.reset();
    opaque_pipeline_.reset();
    skybox_pipeline_.reset();
    depth_attachment_.reset();
}

void ForwardPbrRenderer3D::destroy_all_resources() {
    destroy_swapchain_resources();
    graph_executor_.clear();
    post_material_.reset();
    scene_material_.reset();
    skybox_material_.reset();
    shadow_pass_.reset();
    environment_ = nullptr;
    shadow_depth_is_sampled_ = false;
}

void ForwardPbrRenderer3D::record(const ForwardPbrRenderer3DRenderRequest& request) {
    validate_forward_pbr_renderer_3d_render_request(request);

    const ForwardPbrRenderer3DTargetInfo& target = request.target;
    const ForwardPbrRenderer3DViewInfo& view = request.view;
    const ForwardPbrRenderer3DResourceInfo& resources = request.resources;
    const ForwardPbrRenderer3DSettings& settings = request.settings;

    const math::Vec3 camera_position = camera_world_position(*view.scene, view.camera_entity);
    const LightPacket3D light = forward_pbr_renderer_3d_selected_light(
        view.scene_plan->light_packets, view.light_entity, view.fallback_light);

    scene_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_scene_uniforms({
            .view_projection = view.scene_plan->view_projection_matrix,
            .light_view_projection = view.shadow_plan->view_projection_matrix,
            .camera_position = camera_position,
            .light = light,
            .environment = view.scene_plan->environment,
            .environment_intensity = environment().intensity,
            .prefiltered_mip_levels = environment().prefiltered_mip_levels,
            .environment_rotation_degrees = settings.environment_rotation_degrees,
        }));
    skybox_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_skybox_uniforms({
            .view_projection = view.scene_plan->view_projection_matrix,
            .camera_position = camera_position,
            .environment_intensity = environment().intensity,
            .environment_rotation_degrees = settings.environment_rotation_degrees,
        }));
    post_material().upload(
        target.frame_slot,
        forward_pbr_renderer_3d_post_uniforms({
            .color_format = target.color_target.format,
            .exposure = settings.exposure,
            .tonemap = settings.tonemap,
        }));

    const CompiledGraph render_graph =
        current_render_graph(target.color_target, target.frame_slot, target.color_initial_state,
                             target.color_final_state, *view.shadow_plan, *view.scene_plan,
                             *resources.meshes, *resources.material_instances,
                             *resources.material_factors);
    graph_executor_.record(
        render::RenderGraphFrameRecordInfo{
            .device = target.device,
            .command_buffer = target.command_buffer,
            .frame_slot = target.frame_slot,
            .label = target.command_buffer_label,
            .command_buffer_mode = target.command_buffer_mode,
        },
        render_graph.graph,
        [this, device = target.device, frame_slot = target.frame_slot,
         &render_graph](const render::RenderGraphResourceSet& graph_resources) {
            update_post_descriptor(*device, frame_slot, render_graph.graph, graph_resources,
                                   render_graph.scene_color);
        });
    shadow_depth_is_sampled_ = true;
}

const render::GeneratedPbrEnvironment& ForwardPbrRenderer3D::environment() const {
    if (environment_ == nullptr) {
        throw std::runtime_error("forward PBR renderer environment is not initialized");
    }
    return *environment_;
}

ForwardPbrRenderer3D::CompiledGraph ForwardPbrRenderer3D::current_render_graph(
    render::ColorTargetView color_target, render::FrameSlot frame_slot,
    render::RenderGraphTextureState color_initial_state,
    render::RenderGraphTextureState color_final_state,
    const scene::RenderFramePlan3D& shadow_plan,
    const scene::RenderFramePlan3D& scene_plan,
    const render::MeshResourceTable<render::Mesh>& meshes,
    const render::MaterialResourceTable<render::FrameUniformMaterialInstance<
        render::PbrMaterialUniforms>>& material_instances,
    const std::unordered_map<render::MaterialHandle, render::PbrMaterialFactors,
                             render::MaterialHandleHash>& material_factors) {
    render::RenderGraphBuilder graph;
    const render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("backbuffer", color_target, color_initial_state,
                                  color_final_state);
    const render::RenderGraphTextureHandle scene_color =
        graph.create_texture(render::RenderGraphTextureDesc{
            .label = "scene color",
            .extent = color_target.extent,
            .format = config_.scene_color_format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        });
    const render::RenderGraphTextureHandle scene_depth = graph.import_depth_target(
        "scene depth", render::depth_target_view(depth_attachment()), undefined_texture_state());
    const std::optional<render::RenderGraphTextureState> shadow_initial_state =
        shadow_depth_is_sampled_ ? sampled_depth_texture_state() : undefined_texture_state();
    const render::RenderGraphTextureHandle shadow_depth =
        graph.import_depth_target("shadow depth", shadow_pass().depth_target(),
                                  shadow_initial_state);

    graph.add_pass("shadow", render::RenderGraphQueueDomain::Graphics)
        .write_depth(shadow_depth)
        .material_pass(shadow_pass().material_pass())
        .execute([this, &shadow_plan, &meshes](const render::RenderGraphExecutionContext& context) {
            record_shadow_pass(context.recorder(), shadow_plan, meshes);
        });
    graph.add_pass("scene", render::RenderGraphQueueDomain::Graphics)
        .read_texture(shadow_depth)
        .write_color(scene_color)
        .write_depth(scene_depth)
        .material_pass(render::pbr_forward_pass_info())
        .execute([this, scene_color, frame_slot, &scene_plan, &meshes, &material_instances,
                  &material_factors](
                     const render::RenderGraphExecutionContext& context) {
            const render::ColorTargetView target =
                render::resolved_color_target_view(context, scene_color);
            record_scene_pass(context.recorder(), target, scene_plan, frame_slot, meshes,
                              material_instances, material_factors);
        });
    graph.add_pass("post", render::RenderGraphQueueDomain::Graphics)
        .read_texture(scene_color)
        .write_color(backbuffer)
        .material_pass(render::pbr_post_pass_info())
        .execute([this, color_target, frame_slot](
                     const render::RenderGraphExecutionContext& context) {
            record_post_pass(context.recorder(), color_target, frame_slot);
        });

    return {
        .graph = graph.compile(),
        .scene_color = scene_color,
    };
}

void ForwardPbrRenderer3D::record_shadow_pass(const vulkan::CommandRecorder& recorder,
                                           const scene::RenderFramePlan3D& shadow_plan,
                                           const render::MeshResourceTable<render::Mesh>& meshes)
    const {
    shadow_pass().record(
        recorder, render::depth_clear_value(),
        [this, &shadow_plan, &meshes](const vulkan::CommandRecorder& pass_recorder) {
            scene::record_pipeline_draw_packets_3d(
                pass_recorder, shadow_plan.draw_packets, meshes,
                {
                    .pipeline = &shadow_pass().pipeline(),
                    .filter =
                        {
                            .material_pass = render::MaterialPassKind::DepthOnly,
                            .require_shadow_caster = true,
                        },
                },
                [this, &shadow_plan](const vulkan::CommandRecorder& packet_recorder,
                                     const scene::RenderDrawPacket3D& packet) {
                    packet_recorder.push_constants(
                        shadow_pass().pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                        ShadowPushConstants{
                            .light_mvp =
                                shadow_plan.view_projection_matrix * packet.world_affine_matrix,
                        });
                });
        });
}

void ForwardPbrRenderer3D::record_scene_pass(
    const vulkan::CommandRecorder& recorder, render::ColorTargetView color_target,
    const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
    const render::MeshResourceTable<render::Mesh>& meshes,
    const render::MaterialResourceTable<render::FrameUniformMaterialInstance<
        render::PbrMaterialUniforms>>& material_instances,
    const std::unordered_map<render::MaterialHandle, render::PbrMaterialFactors,
                             render::MaterialHandleHash>& material_factors) const {
    render::record_render_target_pass(
        recorder, render::render_target_view(color_target, render::depth_target_view(
                                                               depth_attachment())),
        config_.scene_clear,
        [this, &scene_plan, &meshes, &material_instances, &material_factors,
         frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            render::record_fullscreen_pipeline_draw(
                pass_recorder,
                render::FullscreenPipelineDrawInfo{
                    .pipeline = &skybox_pipeline(),
                    .descriptor_set = skybox_material().set(frame_slot),
                    .descriptor_set_index = 0,
                });
            const auto record_blend =
                [this, &pass_recorder, &scene_plan, &meshes, &material_instances,
                 &material_factors, frame_slot](const render::GraphicsPipelineResource& pipeline,
                                                render::MaterialBlendMode blend) {
                    scene::record_pipeline_draw_packets_3d(
                        pass_recorder, scene_plan.draw_packets, meshes,
                        {
                            .pipeline = &pipeline,
                            .material = &scene_material().material(),
                            .frame_slot = frame_slot,
                            .filter =
                                {
                                    .material_pass = render::MaterialPassKind::ForwardColor,
                                    .blend_mode = blend,
                                },
                        },
                        [&pipeline, &material_instances, &material_factors,
                         frame_slot](const vulkan::CommandRecorder& packet_recorder,
                                     const scene::RenderDrawPacket3D& packet) {
                            const auto& material = material_instances.at(packet.material);
                            material.upload(frame_slot, render::pbr_material_uniforms(
                                                            material_factors.at(packet.material)));
                            render::bind_material_instance(packet_recorder, pipeline,
                                                           material.material(), frame_slot);
                            packet_recorder.push_constants(
                                pipeline.layout(),
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                render::pbr_push_constants(packet.world_affine_matrix));
                        });
                };
            record_blend(opaque_pipeline(), render::MaterialBlendMode::Opaque);
            record_blend(alpha_pipeline(), render::MaterialBlendMode::AlphaBlend);
        });
}

void ForwardPbrRenderer3D::record_post_pass(const vulkan::CommandRecorder& recorder,
                                            render::ColorTargetView color_target,
                                            render::FrameSlot frame_slot) const {
    render::record_render_target_pass(
        recorder, render::render_target_view(color_target),
        render::RenderClearValues{
            .color = render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
        },
        [this, frame_slot](const vulkan::CommandRecorder& pass_recorder) {
            render::record_fullscreen_pipeline_draw(
                pass_recorder,
                render::FullscreenPipelineDrawInfo{
                    .pipeline = &post_pipeline(),
                    .descriptor_set = post_material().set(frame_slot),
                    .descriptor_set_index = 0,
                });
        });
}

void ForwardPbrRenderer3D::update_post_descriptor(
    const vulkan::Device& device, render::FrameSlot frame_slot,
    const render::CompiledRenderGraph& graph, const render::RenderGraphResourceSet& resources,
    render::RenderGraphTextureHandle scene_color) const {
    const render::RenderGraphSampledTextureView sampled =
        render::resolved_sampled_texture_view(graph, resources, scene_color);
    render::MaterialDescriptorWriter(post_material().set(frame_slot))
        .combined_image_sampler(binding(render::PbrPostBinding::SceneColor),
                                post_sampler().handle(), sampled.view, sampled.layout)
        .update(device);
}

const render::ShadowMapPass3D& ForwardPbrRenderer3D::shadow_pass() const {
    if (!shadow_pass_.has_value()) {
        throw std::runtime_error("forward PBR renderer shadow pass is not initialized");
    }
    return shadow_pass_.value();
}

const render::FrameUniformMaterialInstance<render::PbrSceneUniforms>&
ForwardPbrRenderer3D::scene_material() const {
    if (!scene_material_.has_value()) {
        throw std::runtime_error("forward PBR renderer scene material is not initialized");
    }
    return scene_material_.value();
}

const render::FrameUniformMaterialInstance<render::PbrSkyboxUniforms>&
ForwardPbrRenderer3D::skybox_material() const {
    if (!skybox_material_.has_value()) {
        throw std::runtime_error("forward PBR renderer skybox material is not initialized");
    }
    return skybox_material_.value();
}

const render::FrameUniformMaterialInstance<render::PbrPostUniforms>&
ForwardPbrRenderer3D::post_material() const {
    if (!post_material_.has_value()) {
        throw std::runtime_error("forward PBR renderer post material is not initialized");
    }
    return post_material_.value();
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::opaque_pipeline() const {
    if (!opaque_pipeline_.has_value()) {
        throw std::runtime_error("forward PBR renderer opaque pipeline is not initialized");
    }
    return opaque_pipeline_.value();
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::alpha_pipeline() const {
    if (!alpha_pipeline_.has_value()) {
        throw std::runtime_error("forward PBR renderer alpha pipeline is not initialized");
    }
    return alpha_pipeline_.value();
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::skybox_pipeline() const {
    if (!skybox_pipeline_.has_value()) {
        throw std::runtime_error("forward PBR renderer skybox pipeline is not initialized");
    }
    return skybox_pipeline_.value();
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::post_pipeline() const {
    if (!post_pipeline_.has_value()) {
        throw std::runtime_error("forward PBR renderer post pipeline is not initialized");
    }
    return post_pipeline_.value();
}

const vulkan::Sampler& ForwardPbrRenderer3D::post_sampler() const {
    if (!post_sampler_.has_value()) {
        throw std::runtime_error("forward PBR renderer post sampler is not initialized");
    }
    return post_sampler_.value();
}

const vulkan::DepthAttachment& ForwardPbrRenderer3D::depth_attachment() const {
    if (!depth_attachment_.has_value()) {
        throw std::runtime_error("forward PBR renderer depth attachment is not initialized");
    }
    return depth_attachment_.value();
}

} // namespace cubey
