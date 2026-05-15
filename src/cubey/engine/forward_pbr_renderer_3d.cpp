#include <cubey/engine/forward_pbr_renderer_3d.h>

#include "forward_pbr_renderer_3d_common.h"

#include <cubey/render/pass.h>

#include <glm/matrix.hpp>

#include <cstddef>
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

void validate_forward_pbr_renderer_3d_render_request(
    const ForwardPbrRenderer3DRenderRequest& request) {
    if (request.target.device == nullptr || request.target.command_buffer == VK_NULL_HANDLE) {
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

const render::GeneratedPbrEnvironment& ForwardPbrRenderer3D::environment() const {
    if (environment_ == nullptr) {
        throw std::runtime_error("forward PBR renderer environment is not initialized");
    }
    return *environment_;
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

const render::GraphicsPipelineResource& ForwardPbrRenderer3D::mask_shadow_pipeline() const {
    if (!mask_shadow_pipeline_.has_value()) {
        throw std::runtime_error("forward PBR renderer mask shadow pipeline is not initialized");
    }
    return mask_shadow_pipeline_.value();
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
