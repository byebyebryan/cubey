#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_internal.h"

#include <cubey/render/pass.h>

#include <glm/matrix.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cubey {

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
    if (config.shadow_depth_fragment_shader.empty()) {
        throw std::runtime_error("forward PBR renderer requires a shadow depth fragment shader");
    }
    if (config.shadow_extent == 0) {
        throw std::runtime_error("forward PBR renderer requires a nonzero shadow extent");
    }
    if (config.scene_color_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("forward PBR renderer requires a scene color format");
    }
}

ForwardPbrRenderer3DConfig
forward_pbr_renderer_3d_config_from_shader_directory(std::filesystem::path shader_directory,
                                                     ForwardPbrRenderer3DConfig base) {
    if (shader_directory.empty()) {
        throw std::runtime_error("forward PBR renderer requires a shader directory");
    }
    base.pbr_vertex_shader = shader_directory / "forward_pbr.vert.spv";
    base.pbr_fragment_shader = shader_directory / "forward_pbr.frag.spv";
    base.skybox_vertex_shader = shader_directory / "forward_pbr_skybox.vert.spv";
    base.skybox_fragment_shader = shader_directory / "forward_pbr_skybox.frag.spv";
    base.atmosphere_vertex_shader = shader_directory / "atmosphere.vert.spv";
    base.atmosphere_fragment_shader = shader_directory / "atmosphere.frag.spv";
    base.post_vertex_shader = shader_directory / "forward_pbr_post.vert.spv";
    base.post_fragment_shader = shader_directory / "forward_pbr_post.frag.spv";
    base.shadow_depth_vertex_shader = shader_directory / "forward_pbr_shadow_depth.vert.spv";
    base.shadow_depth_fragment_shader = shader_directory / "forward_pbr_shadow_depth.frag.spv";
    return base;
}

ForwardPbrRenderer3DRenderRequest
forward_pbr_renderer_3d_render_request(const ForwardPbrRenderer3DFrameRequestInfo& info) {
    return {
        .target =
            {
                .device = info.device,
                .command_buffer = info.command_buffer,
                .color_target = info.color_target,
                .frame_slot = info.frame_slot,
                .color_initial_state = info.color_initial_state,
                .color_final_state = info.color_final_state,
                .command_buffer_label = info.command_buffer_label,
                .command_buffer_mode = info.command_buffer_mode,
                .profiler = info.profiler,
            },
        .view =
            {
                .scene = info.scene,
                .frame_plan = info.frame_plan,
                .camera_entity = info.camera_entity,
                .light_entity = info.light_entity,
                .fallback_light = info.fallback_light,
            },
        .scene_resources = info.scene_resources,
        .settings = info.settings,
    };
}

void validate_forward_pbr_renderer_3d_render_request(
    const ForwardPbrRenderer3DRenderRequest& request) {
    if (request.target.device == nullptr || request.target.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("forward PBR render request requires device and command buffer");
    }
    if (request.view.scene == nullptr || request.view.frame_plan == nullptr) {
        throw std::runtime_error("forward PBR render request requires scene and frame plans");
    }
    (void)forward_pbr_renderer_3d_frame_plans(*request.view.frame_plan);
    if (request.scene_resources.meshes == nullptr || request.scene_resources.materials == nullptr) {
        throw std::runtime_error("forward PBR render request requires scene resources");
    }
    if (request.settings.background_mode == ForwardPbrRenderer3DBackgroundMode::Atmosphere &&
        !request.settings.atmosphere_background.has_value()) {
        throw std::runtime_error(
            "forward PBR atmosphere background requires atmosphere frame uniforms");
    }
    if (request.settings.atmosphere_clouds.has_value()) {
        if (request.settings.background_mode != ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
            throw std::runtime_error(
                "forward PBR atmosphere clouds require the atmosphere background");
        }
        if (request.settings.atmosphere_clouds->runtime == nullptr) {
            throw std::runtime_error("forward PBR atmosphere clouds require a runtime");
        }
    }
    if (request.settings.terrain_backdrop.has_value() &&
        request.settings.ocean_surface.has_value()) {
        throw std::runtime_error(
            "forward PBR v1 does not compose terrain and ocean simultaneously");
    }
    if (request.settings.terrain_backdrop.has_value()) {
        if (request.settings.background_mode != ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
            throw std::runtime_error(
                "forward PBR terrain backdrop requires the atmosphere background");
        }
        const TerrainBackdropRuntime* runtime = request.settings.terrain_backdrop->runtime;
        if (runtime == nullptr || !runtime->created() || !runtime->target_resources_created()) {
            throw std::runtime_error("forward PBR terrain backdrop requires a complete runtime");
        }
    }
    if (request.settings.ocean_surface.has_value()) {
        if (request.settings.background_mode != ForwardPbrRenderer3DBackgroundMode::Atmosphere) {
            throw std::runtime_error(
                "forward PBR ocean surface requires the atmosphere background");
        }
        const OceanSurfaceRuntime* runtime = request.settings.ocean_surface->runtime;
        if (runtime == nullptr || !runtime->initialized()) {
            throw std::runtime_error("forward PBR ocean surface requires a complete runtime");
        }
    }
}

ForwardPbrRenderer3DFramePlans
forward_pbr_renderer_3d_frame_plans(const scene::FrameRenderPlan3D& frame_plan) {
    ForwardPbrRenderer3DFramePlans result;
    for (const scene::RenderPassPlan3D& pass : frame_plan.passes()) {
        switch (pass.kind) {
        case scene::RenderPassKind3D::DepthOnly:
            if (result.shadow != nullptr) {
                throw std::runtime_error("forward PBR frame plan has duplicate shadow passes");
            }
            result.shadow = &pass.frame_plan;
            break;
        case scene::RenderPassKind3D::Color:
            if (result.scene != nullptr) {
                throw std::runtime_error("forward PBR frame plan has duplicate scene passes");
            }
            result.scene = &pass.frame_plan;
            break;
        }
    }
    if (result.shadow == nullptr || result.scene == nullptr) {
        throw std::runtime_error("forward PBR frame plan requires one shadow and one scene pass");
    }
    return result;
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
    return {
        .vertex_bindings =
            {
                render::vertex_input_binding(0, sizeof(render::PbrVertex),
                                             VK_VERTEX_INPUT_RATE_VERTEX),
            },
        .attributes =
            {
                render::vertex_input_attribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                               offsetof(render::PbrVertex, position)),
                render::vertex_input_attribute(3, 0, VK_FORMAT_R32G32_SFLOAT,
                                               offsetof(render::PbrVertex, uv0)),
                render::vertex_input_attribute(4, 0, VK_FORMAT_R32G32_SFLOAT,
                                               offsetof(render::PbrVertex, uv1)),
                render::vertex_input_attribute(5, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                               offsetof(render::PbrVertex, color0)),
            },
    };
}

render::PbrSceneUniforms
forward_pbr_renderer_3d_scene_uniforms(const ForwardPbrRenderer3DSceneUniformInfo& info) {
    const math::Vec3 ambient = info.environment.ambient_color * info.environment.ambient_intensity;
    const float radians =
        forward_pbr_renderer_3d_rotation_radians(info.environment_rotation_degrees);
    std::array<math::Vec4, 9> diffuse_irradiance_sh{};
    for (std::size_t index = 0; index < diffuse_irradiance_sh.size(); ++index) {
        const math::Vec3& coefficient = info.environment.diffuse_irradiance_sh[index];
        diffuse_irradiance_sh[index] = {coefficient.x, coefficient.y, coefficient.z, 0.0F};
    }
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
        .debug_options = {static_cast<float>(info.debug_view), 0.0F, 0.0F, 0.0F},
        .diffuse_irradiance_sh = diffuse_irradiance_sh,
        .environment_options =
            {
                info.environment.diffuse_irradiance_sh_enabled ? 1.0F : 0.0F,
                info.environment_blend,
                0.0F,
                0.0F,
            },
        .backdrop_reflection_radiance_strength = {info.backdrop_reflection.radiance,
                                                  info.backdrop_reflection.strength},
        .backdrop_reflection_horizon = {info.backdrop_reflection.horizon_elevation_sine,
                                        info.backdrop_reflection.horizon_softness, 0.0F, 0.0F},
    };
}

render::PbrSkyboxUniforms
forward_pbr_renderer_3d_skybox_uniforms(const ForwardPbrRenderer3DSkyboxUniformInfo& info) {
    const float radians =
        forward_pbr_renderer_3d_rotation_radians(info.environment_rotation_degrees);
    return {
        .inverse_view_projection = glm::inverse(info.view_projection),
        .camera_position = {info.camera_position, 1.0F},
        .environment_rotation_intensity =
            {
                std::cos(radians),
                std::sin(radians),
                info.environment_intensity,
                info.environment_blend,
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
    : impl_(std::make_unique<Impl>(std::move(config))) {}

ForwardPbrRenderer3D::~ForwardPbrRenderer3D() = default;

ForwardPbrRenderer3D::Impl::Impl(ForwardPbrRenderer3DConfig config) : config_(std::move(config)) {
    validate_forward_pbr_renderer_3d_config(config_);
}

bool ForwardPbrRenderer3D::Impl::has_global_resources() const {
    return global_.environment_initialized || global_.graph_executor.frame_slot_count() != 0 ||
           global_.shadow_pass.has_value() || global_.shadow_double_sided_pipeline.has_value() ||
           global_.scene_material.has_value() || global_.skybox_material.has_value() ||
           global_.atmosphere_background.materials_created() || global_.post_material.has_value();
}

bool ForwardPbrRenderer3D::Impl::has_swapchain_resources() const {
    if (swapchain_.depth_attachment.has_value() || swapchain_.post_sampler.has_value() ||
        swapchain_.skybox_pipeline.has_value() || swapchain_.post_pipeline.has_value()) {
        return true;
    }
    for (std::size_t index = 0; index < swapchain_.pipeline_variants.size(); ++index) {
        if (index == static_cast<std::size_t>(ForwardPbrPipelineVariant::ShadowDoubleSided)) {
            continue;
        }
        if (swapchain_.pipeline_variants[index].has_value()) {
            return true;
        }
    }
    return false;
}

void ForwardPbrRenderer3D::Impl::require_global_resources() const {
    if (!has_global_resources()) {
        throw std::runtime_error("forward PBR renderer global resources are not initialized");
    }
}

void ForwardPbrRenderer3D::Impl::require_swapchain_resources() const {
    require_global_resources();
    const auto has_pipeline = [this](ForwardPbrPipelineVariant variant) {
        return swapchain_.pipeline_variants[static_cast<std::size_t>(variant)].has_value();
    };
    if (!swapchain_.depth_attachment.has_value() || !swapchain_.post_sampler.has_value() ||
        !swapchain_.skybox_pipeline.has_value() || !swapchain_.post_pipeline.has_value() ||
        !has_pipeline(ForwardPbrPipelineVariant::Opaque) ||
        !has_pipeline(ForwardPbrPipelineVariant::OpaqueDoubleSided) ||
        !has_pipeline(ForwardPbrPipelineVariant::Alpha) ||
        !has_pipeline(ForwardPbrPipelineVariant::AlphaDoubleSided) ||
        !has_pipeline(ForwardPbrPipelineVariant::MaskShadow) ||
        !has_pipeline(ForwardPbrPipelineVariant::MaskShadowDoubleSided)) {
        throw std::runtime_error("forward PBR renderer swapchain resources are not initialized");
    }
}

void ForwardPbrRenderer3D::Impl::require_atmosphere_background_resources() const {
    if (!global_.atmosphere_background.materials_created()) {
        throw std::runtime_error("forward PBR atmosphere background resources are not initialized");
    }
    static_cast<void>(global_.atmosphere_background.pipeline());
}

void ForwardPbrRenderer3D::Impl::require_no_global_resources() const {
    if (has_global_resources()) {
        throw std::runtime_error("forward PBR renderer global resources are already initialized");
    }
}

void ForwardPbrRenderer3D::Impl::require_no_swapchain_resources() const {
    if (has_swapchain_resources()) {
        throw std::runtime_error(
            "forward PBR renderer swapchain resources are already initialized");
    }
}

const render::ShadowMapPass3D& ForwardPbrRenderer3D::Impl::shadow_pass() const {
    if (!global_.shadow_pass.has_value()) {
        throw std::runtime_error("forward PBR renderer shadow pass is not initialized");
    }
    return global_.shadow_pass.value();
}

const render::FrameUniformMaterialInstance<render::PbrSceneUniforms>&
ForwardPbrRenderer3D::Impl::scene_material() const {
    if (!global_.scene_material.has_value()) {
        throw std::runtime_error("forward PBR renderer scene material is not initialized");
    }
    return global_.scene_material.value();
}

const render::FrameUniformMaterialInstance<render::PbrSkyboxUniforms>&
ForwardPbrRenderer3D::Impl::skybox_material() const {
    if (!global_.skybox_material.has_value()) {
        throw std::runtime_error("forward PBR renderer skybox material is not initialized");
    }
    return global_.skybox_material.value();
}

const render::FrameUniformMaterialInstance<render::PbrPostUniforms>&
ForwardPbrRenderer3D::Impl::post_material() const {
    if (!global_.post_material.has_value()) {
        throw std::runtime_error("forward PBR renderer post material is not initialized");
    }
    return global_.post_material.value();
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::Impl::opaque_pipeline() const {
    return pipeline_variant(ForwardPbrPipelineVariant::Opaque);
}

const render::GraphicsPipelineResource&
ForwardPbrRenderer3D::Impl::opaque_double_sided_pipeline() const {
    return pipeline_variant(ForwardPbrPipelineVariant::OpaqueDoubleSided);
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::Impl::alpha_pipeline() const {
    return pipeline_variant(ForwardPbrPipelineVariant::Alpha);
}

const render::GraphicsPipelineResource&
ForwardPbrRenderer3D::Impl::alpha_double_sided_pipeline() const {
    return pipeline_variant(ForwardPbrPipelineVariant::AlphaDoubleSided);
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::Impl::mask_shadow_pipeline() const {
    return pipeline_variant(ForwardPbrPipelineVariant::MaskShadow);
}

const render::GraphicsPipelineResource&
ForwardPbrRenderer3D::Impl::mask_shadow_double_sided_pipeline() const {
    return pipeline_variant(ForwardPbrPipelineVariant::MaskShadowDoubleSided);
}

const render::GraphicsPipelineResource&
ForwardPbrRenderer3D::Impl::shadow_double_sided_pipeline() const {
    return pipeline_variant(ForwardPbrPipelineVariant::ShadowDoubleSided);
}

std::optional<render::GraphicsPipelineResource>&
ForwardPbrRenderer3D::Impl::pipeline_variant_slot(ForwardPbrPipelineVariant variant) {
    if (variant == ForwardPbrPipelineVariant::ShadowDoubleSided) {
        return global_.shadow_double_sided_pipeline;
    }
    return swapchain_.pipeline_variants[static_cast<std::size_t>(variant)];
}

const render::GraphicsPipelineResource&
ForwardPbrRenderer3D::Impl::pipeline_variant(ForwardPbrPipelineVariant variant) const {
    if (variant == ForwardPbrPipelineVariant::ShadowDoubleSided) {
        if (!global_.shadow_double_sided_pipeline.has_value()) {
            throw std::runtime_error("forward PBR renderer pipeline variant is not initialized");
        }
        return global_.shadow_double_sided_pipeline.value();
    }
    const std::optional<render::GraphicsPipelineResource>& pipeline =
        swapchain_.pipeline_variants[static_cast<std::size_t>(variant)];
    if (!pipeline.has_value()) {
        throw std::runtime_error("forward PBR renderer pipeline variant is not initialized");
    }
    return pipeline.value();
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::Impl::skybox_pipeline() const {
    if (!swapchain_.skybox_pipeline.has_value()) {
        throw std::runtime_error("forward PBR renderer skybox pipeline is not initialized");
    }
    return swapchain_.skybox_pipeline.value();
}

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::Impl::post_pipeline() const {
    if (!swapchain_.post_pipeline.has_value()) {
        throw std::runtime_error("forward PBR renderer post pipeline is not initialized");
    }
    return swapchain_.post_pipeline.value();
}

const vulkan::Sampler& ForwardPbrRenderer3D::Impl::post_sampler() const {
    if (!swapchain_.post_sampler.has_value()) {
        throw std::runtime_error("forward PBR renderer post sampler is not initialized");
    }
    return swapchain_.post_sampler.value();
}

const vulkan::DepthAttachment& ForwardPbrRenderer3D::Impl::depth_attachment() const {
    if (!swapchain_.depth_attachment.has_value()) {
        throw std::runtime_error("forward PBR renderer depth attachment is not initialized");
    }
    return swapchain_.depth_attachment.value();
}

} // namespace cubey
