#pragma once

#include <cubey/core/math.h>
#include <cubey/engine/cloud_environment_runtime.h>
#include <cubey/engine/terrain_backdrop_runtime.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/deformation.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace cubey::vulkan {
class GpuTimestampProfiler;
}

namespace cubey {

struct ForwardPbrRenderer3DConfig {
    std::filesystem::path pbr_vertex_shader{};
    std::filesystem::path pbr_fragment_shader{};
    std::filesystem::path skybox_vertex_shader{};
    std::filesystem::path skybox_fragment_shader{};
    std::filesystem::path atmosphere_vertex_shader{};
    std::filesystem::path atmosphere_fragment_shader{};
    std::filesystem::path post_vertex_shader{};
    std::filesystem::path post_fragment_shader{};
    std::filesystem::path shadow_depth_vertex_shader{};
    std::filesystem::path shadow_depth_fragment_shader{};
    std::uint32_t shadow_extent = 2048;
    VkFormat shadow_depth_format = VK_FORMAT_UNDEFINED;
    VkFormat scene_color_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    render::RenderClearValues scene_clear{
        .color = render::color_clear_value(0.018F, 0.020F, 0.026F, 1.0F),
        .depth = render::depth_clear_value(),
    };
};

struct ForwardPbrRenderer3DGlobalResourcesInfo {
    render::PbrEnvironmentTextureBindings environment_textures{};
    std::uint32_t frame_slot_count = 1;
    std::optional<render::AtmosphereBackgroundTextureBindings> atmosphere_background_textures{};
};

struct ForwardPbrRenderer3DTargetResourcesInfo {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    const render::PbrMaterialTable* materials = nullptr;
};

struct ForwardPbrRenderer3DSceneUniformInfo {
    math::Mat4 view_projection{1.0F};
    math::Mat4 light_view_projection{1.0F};
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    LightPacket3D light{};
    scene::Environment3D environment{};
    float environment_intensity = 1.0F;
    std::uint32_t prefiltered_mip_levels = 1;
    float environment_blend = 1.0F;
    float environment_rotation_degrees = 0.0F;
    render::PbrDebugView debug_view = render::PbrDebugView::Final;
};

struct ForwardPbrRenderer3DSkyboxUniformInfo {
    math::Mat4 view_projection{1.0F};
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    float environment_intensity = 1.0F;
    float environment_blend = 1.0F;
    float environment_rotation_degrees = 0.0F;
};

struct ForwardPbrRenderer3DPostUniformInfo {
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    float exposure = 0.0F;
    render::PbrTonemap tonemap = render::PbrTonemap::Aces;
};

struct ForwardPbrRenderer3DTargetInfo {
    const vulkan::Device* device = nullptr;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    render::ColorTargetView color_target{};
    render::FrameSlot frame_slot{};
    render::RenderGraphTextureState color_initial_state{};
    render::RenderGraphTextureState color_final_state{};
    const char* command_buffer_label = "vkEndCommandBuffer forward pbr renderer";
    render::RenderGraphCommandBufferMode command_buffer_mode =
        render::RenderGraphCommandBufferMode::BeginAndEnd;
    vulkan::GpuTimestampProfiler* profiler = nullptr;
};

struct ForwardPbrRenderer3DViewInfo {
    const SceneReadView* scene = nullptr;
    const scene::FrameRenderPlan3D* frame_plan = nullptr;
    Entity camera_entity{};
    Entity light_entity{};
    LightPacket3D fallback_light{};
};

struct ForwardPbrRenderer3DSceneResources {
    const render::MeshResourceTable<render::Mesh>* meshes = nullptr;
    const render::FrameMeshResourceTable* frame_meshes = nullptr;
    std::span<const render::GpuDeformationCommand> deformation_commands{};
    const render::PbrMaterialTable* materials = nullptr;
};

enum class ForwardPbrRenderer3DBackgroundMode : std::uint8_t {
    IblSkybox,
    Atmosphere,
};

struct ForwardPbrRenderer3DAtmosphereClouds {
    CloudEnvironmentRuntime* runtime = nullptr;
    CloudEnvironmentRuntimeFrame frame{};
};

struct ForwardPbrRenderer3DTerrainBackdrop {
    TerrainBackdropRuntime* runtime = nullptr;
    TerrainBackdropRuntimeFrameInfo frame{};
};

struct ForwardPbrRenderer3DSettings {
    float environment_rotation_degrees = 0.0F;
    float exposure = 0.0F;
    render::PbrTonemap tonemap = render::PbrTonemap::Aces;
    render::PbrDebugView debug_view = render::PbrDebugView::Final;
    ForwardPbrRenderer3DBackgroundMode background_mode =
        ForwardPbrRenderer3DBackgroundMode::IblSkybox;
    std::optional<render::AtmosphereEnvironmentFrameUniforms> atmosphere_background{};
    std::optional<ForwardPbrRenderer3DAtmosphereClouds> atmosphere_clouds{};
    std::optional<ForwardPbrRenderer3DTerrainBackdrop> terrain_backdrop{};
};

struct ForwardPbrRenderer3DSceneTargetInfo {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
};

struct ForwardPbrRenderer3DRenderRequest {
    ForwardPbrRenderer3DTargetInfo target{};
    ForwardPbrRenderer3DViewInfo view{};
    ForwardPbrRenderer3DSceneResources scene_resources{};
    ForwardPbrRenderer3DSettings settings{};
};

struct ForwardPbrRenderer3DFrameRequestInfo {
    const vulkan::Device* device = nullptr;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    render::ColorTargetView color_target{};
    render::FrameSlot frame_slot{};
    render::RenderGraphTextureState color_initial_state{};
    render::RenderGraphTextureState color_final_state{};
    const char* command_buffer_label = "vkEndCommandBuffer forward pbr renderer";
    render::RenderGraphCommandBufferMode command_buffer_mode =
        render::RenderGraphCommandBufferMode::BeginAndEnd;
    vulkan::GpuTimestampProfiler* profiler = nullptr;
    const SceneReadView* scene = nullptr;
    const scene::FrameRenderPlan3D* frame_plan = nullptr;
    Entity camera_entity{};
    Entity light_entity{};
    LightPacket3D fallback_light{};
    ForwardPbrRenderer3DSceneResources scene_resources{};
    ForwardPbrRenderer3DSettings settings{};
};

struct ForwardPbrRenderer3DFramePlans {
    const scene::RenderFramePlan3D* shadow = nullptr;
    const scene::RenderFramePlan3D* scene = nullptr;
};

void validate_forward_pbr_renderer_3d_config(const ForwardPbrRenderer3DConfig& config);
[[nodiscard]] ForwardPbrRenderer3DConfig
forward_pbr_renderer_3d_config_from_shader_directory(std::filesystem::path shader_directory,
                                                     ForwardPbrRenderer3DConfig base = {});
[[nodiscard]] ForwardPbrRenderer3DRenderRequest
forward_pbr_renderer_3d_render_request(const ForwardPbrRenderer3DFrameRequestInfo& info);
void validate_forward_pbr_renderer_3d_render_request(
    const ForwardPbrRenderer3DRenderRequest& request);
[[nodiscard]] ForwardPbrRenderer3DFramePlans
forward_pbr_renderer_3d_frame_plans(const scene::FrameRenderPlan3D& frame_plan);
[[nodiscard]] render::VertexInputLayout forward_pbr_renderer_3d_shadow_vertex_input_layout();
[[nodiscard]] LightPacket3D
forward_pbr_renderer_3d_selected_light(std::span<const LightPacket3D> lights,
                                       Entity requested_light, LightPacket3D fallback_light);
[[nodiscard]] render::PbrSceneUniforms
forward_pbr_renderer_3d_scene_uniforms(const ForwardPbrRenderer3DSceneUniformInfo& info);
[[nodiscard]] render::PbrSkyboxUniforms
forward_pbr_renderer_3d_skybox_uniforms(const ForwardPbrRenderer3DSkyboxUniformInfo& info);
[[nodiscard]] render::PbrPostUniforms
forward_pbr_renderer_3d_post_uniforms(const ForwardPbrRenderer3DPostUniformInfo& info);

class ForwardPbrRenderer3D {
  public:
    explicit ForwardPbrRenderer3D(ForwardPbrRenderer3DConfig config);
    ~ForwardPbrRenderer3D();

    ForwardPbrRenderer3D(const ForwardPbrRenderer3D&) = delete;
    ForwardPbrRenderer3D& operator=(const ForwardPbrRenderer3D&) = delete;
    ForwardPbrRenderer3D(ForwardPbrRenderer3D&&) = delete;
    ForwardPbrRenderer3D& operator=(ForwardPbrRenderer3D&&) = delete;

    void create_global_resources(const vulkan::Device& device,
                                 const render::GeneratedPbrEnvironment& environment,
                                 std::uint32_t frame_slot_count);
    void create_global_resources(const vulkan::Device& device,
                                 const ForwardPbrRenderer3DGlobalResourcesInfo& info);
    void create_swapchain_resources(const vulkan::Device& device,
                                    const ForwardPbrRenderer3DTargetResourcesInfo& info);
    void update_environment(const vulkan::Device& device, render::FrameSlot frame_slot,
                            const render::PbrEnvironmentTextureBindings& environment);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    [[nodiscard]] ForwardPbrRenderer3DSceneTargetInfo scene_target_info() const;
    void record(const ForwardPbrRenderer3DFrameRequestInfo& info);
    void record(const ForwardPbrRenderer3DRenderRequest& request);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cubey
