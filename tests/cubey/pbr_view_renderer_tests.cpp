#include <cubey/engine/pbr_view_renderer.h>

#include <vulkan/vulkan.h>

#include <cmath>
#include <span>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

cubey::PbrViewRenderer3DConfig valid_config() {
    return {
        .pbr_vertex_shader = "pbr.vert.spv",
        .pbr_fragment_shader = "pbr.frag.spv",
        .skybox_vertex_shader = "skybox.vert.spv",
        .skybox_fragment_shader = "skybox.frag.spv",
        .shadow_depth_vertex_shader = "shadow.vert.spv",
    };
}

} // namespace

void test_pbr_view_renderer_config_requires_shader_paths_and_shadow_extent() {
    require_throws([] { cubey::validate_pbr_view_renderer_config({}); },
                   "PBR view renderer config should reject missing shader paths");

    cubey::PbrViewRenderer3DConfig config = valid_config();
    config.shadow_extent = 0;
    require_throws([&config] { cubey::validate_pbr_view_renderer_config(config); },
                   "PBR view renderer config should reject zero shadow extent");

    config.shadow_extent = 1024;
    cubey::validate_pbr_view_renderer_config(config);
}

void test_pbr_view_renderer_selects_requested_light_or_fallback() {
    const cubey::Entity requested{.index = 2, .generation = 1};
    const cubey::Entity other{.index = 3, .generation = 1};
    const cubey::LightPacket3D fallback{
        .entity = cubey::Entity{.index = 9, .generation = 1},
        .color = {0.1F, 0.2F, 0.3F},
        .intensity = 4.0F,
        .direction = {0.0F, -1.0F, 0.0F},
    };
    const cubey::LightPacket3D packets[]{
        cubey::LightPacket3D{
            .entity = other,
            .color = {1.0F, 0.0F, 0.0F},
            .intensity = 1.0F,
        },
        cubey::LightPacket3D{
            .entity = requested,
            .color = {0.7F, 0.8F, 0.9F},
            .intensity = 2.5F,
            .direction = {1.0F, 0.0F, 0.0F},
        },
    };

    const cubey::LightPacket3D selected =
        cubey::pbr_view_selected_light(packets, requested, fallback);
    require(selected.entity == requested, "PBR view renderer should select the requested light");
    require(selected.color == cubey::math::Vec3{0.7F, 0.8F, 0.9F},
            "PBR view renderer should preserve selected light color");
    require(selected.intensity == 2.5F,
            "PBR view renderer should preserve selected light intensity");

    const cubey::LightPacket3D missing =
        cubey::pbr_view_selected_light(std::span<const cubey::LightPacket3D>{}, requested,
                                       fallback);
    require(missing.entity == fallback.entity,
            "PBR view renderer should fall back when the requested light is absent");
    require(missing.intensity == fallback.intensity,
            "PBR view renderer should preserve fallback light intensity");
}

void test_pbr_view_renderer_scene_uniforms_pack_view_light_environment_and_display() {
    const cubey::LightPacket3D light{
        .entity = cubey::Entity{.index = 2, .generation = 1},
        .color = {0.6F, 0.7F, 0.8F},
        .intensity = 3.5F,
        .direction = {0.0F, -1.0F, 0.0F},
    };
    const cubey::render::PbrSceneUniforms uniforms = cubey::pbr_view_scene_uniforms({
        .view_projection = cubey::math::Mat4{2.0F},
        .light_view_projection = cubey::math::Mat4{3.0F},
        .camera_position = {1.0F, 2.0F, 3.0F},
        .light = light,
        .environment =
            cubey::scene::Environment3D{
                .ambient_color = {0.2F, 0.3F, 0.4F},
                .ambient_intensity = 2.0F,
            },
        .environment_intensity = 5.0F,
        .prefiltered_mip_levels = 6,
        .environment_rotation_degrees = 90.0F,
        .color_format = VK_FORMAT_R8G8B8A8_UNORM,
        .exposure = 1.25F,
    });

    require(uniforms.view_projection == cubey::math::Mat4{2.0F},
            "PBR view scene uniforms should preserve scene view-projection");
    require(uniforms.light_view_projection == cubey::math::Mat4{3.0F},
            "PBR view scene uniforms should preserve light view-projection");
    require(uniforms.camera_position == cubey::math::Vec4{1.0F, 2.0F, 3.0F, 1.0F},
            "PBR view scene uniforms should pack camera position");
    require(uniforms.light_color_intensity ==
                cubey::math::Vec4{0.6F, 0.7F, 0.8F, 3.5F},
            "PBR view scene uniforms should pack light color and intensity");
    require(uniforms.ambient_color_intensity ==
                cubey::math::Vec4{0.4F, 0.6F, 0.8F, 1.0F},
            "PBR view scene uniforms should pack ambient color times intensity");
    require(uniforms.environment_intensity_mip_count.x == 5.0F,
            "PBR view scene uniforms should pack environment intensity");
    require(uniforms.environment_intensity_mip_count.y == 6.0F,
            "PBR view scene uniforms should pack prefiltered mip count");
    require_near(uniforms.environment_intensity_mip_count.z, 0.0F,
                 "PBR view scene uniforms should pack rotation cosine");
    require_near(uniforms.environment_intensity_mip_count.w, 1.0F,
                 "PBR view scene uniforms should pack rotation sine");
    require(uniforms.display_transform == cubey::math::Vec4{1.25F, 1.0F, 1.0F, 0.0F},
            "PBR view scene uniforms should pack display transform");
}

void test_pbr_view_renderer_skybox_uniforms_pack_inverse_view_camera_environment_and_display() {
    const cubey::render::PbrSkyboxUniforms uniforms = cubey::pbr_view_skybox_uniforms({
        .view_projection = cubey::math::Mat4{1.0F},
        .camera_position = {4.0F, 5.0F, 6.0F},
        .environment_intensity = 2.25F,
        .environment_rotation_degrees = 180.0F,
        .color_format = VK_FORMAT_B8G8R8A8_SRGB,
        .exposure = -0.5F,
    });

    require(uniforms.inverse_view_projection == cubey::math::Mat4{1.0F},
            "PBR view skybox uniforms should invert the view-projection matrix");
    require(uniforms.camera_position == cubey::math::Vec4{4.0F, 5.0F, 6.0F, 1.0F},
            "PBR view skybox uniforms should pack camera position");
    require_near(uniforms.environment_rotation_intensity.x, -1.0F,
                 "PBR view skybox uniforms should pack rotation cosine");
    require_near(uniforms.environment_rotation_intensity.y, 0.0F,
                 "PBR view skybox uniforms should pack rotation sine");
    require(uniforms.environment_rotation_intensity.z == 2.25F,
            "PBR view skybox uniforms should pack environment intensity");
    require(uniforms.display_transform == cubey::math::Vec4{-0.5F, 1.0F, 0.0F, 0.0F},
            "PBR view skybox uniforms should pack display transform");
}
