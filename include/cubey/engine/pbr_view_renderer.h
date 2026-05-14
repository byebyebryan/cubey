#pragma once

#include <cubey/core/math.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/shadow_map.h>
#include <cubey/render/target.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <unordered_map>

namespace cubey {

struct PbrViewRenderer3DConfig {
    std::filesystem::path pbr_vertex_shader{};
    std::filesystem::path pbr_fragment_shader{};
    std::filesystem::path skybox_vertex_shader{};
    std::filesystem::path skybox_fragment_shader{};
    std::filesystem::path shadow_depth_vertex_shader{};
    std::uint32_t shadow_extent = 2048;
    VkFormat shadow_depth_format = VK_FORMAT_UNDEFINED;
    render::RenderClearValues scene_clear{
        .color = render::color_clear_value(0.018F, 0.020F, 0.026F, 1.0F),
        .depth = render::depth_clear_value(),
    };
};

struct PbrViewRenderer3DSwapchainResourcesInfo {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkDescriptorSetLayout material_descriptor_set_layout = VK_NULL_HANDLE;
};

struct PbrViewSceneUniformInfo {
    math::Mat4 view_projection{1.0F};
    math::Mat4 light_view_projection{1.0F};
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    LightPacket3D light{};
    scene::Environment3D environment{};
    float environment_intensity = 1.0F;
    std::uint32_t prefiltered_mip_levels = 1;
    float environment_rotation_degrees = 0.0F;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    float exposure = 0.0F;
    render::PbrTonemap tonemap = render::PbrTonemap::Aces;
};

struct PbrViewSkyboxUniformInfo {
    math::Mat4 view_projection{1.0F};
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    float environment_intensity = 1.0F;
    float environment_rotation_degrees = 0.0F;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    float exposure = 0.0F;
    render::PbrTonemap tonemap = render::PbrTonemap::Aces;
};

struct PbrViewRenderer3DRecordInfo {
    const vulkan::Device* device = nullptr;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    render::ColorTargetView color_target{};
    render::FrameSlot frame_slot{};
    render::RenderGraphTextureState color_initial_state{};
    render::RenderGraphTextureState color_final_state{};
    const SceneReadView* scene = nullptr;
    const scene::RenderFramePlan3D* shadow_plan = nullptr;
    const scene::RenderFramePlan3D* scene_plan = nullptr;
    const render::MeshResourceTable<render::Mesh>* meshes = nullptr;
    const render::MaterialResourceTable<render::FrameUniformMaterialInstance<
        render::PbrMaterialUniforms>>* material_instances = nullptr;
    const std::unordered_map<render::MaterialHandle, render::PbrMaterialFactors,
                             render::MaterialHandleHash>* material_factors = nullptr;
    Entity camera_entity{};
    Entity light_entity{};
    LightPacket3D fallback_light{};
    float environment_rotation_degrees = 0.0F;
    float exposure = 0.0F;
    const char* command_buffer_label = "vkEndCommandBuffer pbr view renderer";
};

void validate_pbr_view_renderer_config(const PbrViewRenderer3DConfig& config);
[[nodiscard]] LightPacket3D pbr_view_selected_light(std::span<const LightPacket3D> lights,
                                                    Entity requested_light,
                                                    LightPacket3D fallback_light);
[[nodiscard]] render::PbrSceneUniforms
pbr_view_scene_uniforms(const PbrViewSceneUniformInfo& info);
[[nodiscard]] render::PbrSkyboxUniforms
pbr_view_skybox_uniforms(const PbrViewSkyboxUniformInfo& info);

class PbrViewRenderer3D {
  public:
    explicit PbrViewRenderer3D(PbrViewRenderer3DConfig config);

    PbrViewRenderer3D(const PbrViewRenderer3D&) = delete;
    PbrViewRenderer3D& operator=(const PbrViewRenderer3D&) = delete;
    PbrViewRenderer3D(PbrViewRenderer3D&&) = delete;
    PbrViewRenderer3D& operator=(PbrViewRenderer3D&&) = delete;

    void create_global_resources(const vulkan::Device& device,
                                 const render::GeneratedPbrEnvironment& environment,
                                 std::uint32_t frame_slot_count);
    void create_swapchain_resources(const vulkan::Device& device,
                                    const PbrViewRenderer3DSwapchainResourcesInfo& info);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    void record(const PbrViewRenderer3DRecordInfo& info);

    [[nodiscard]] const render::GeneratedPbrEnvironment& environment() const;

  private:
    struct CompiledGraph {
        render::CompiledRenderGraph graph;
    };

    [[nodiscard]] CompiledGraph current_render_graph(
        render::ColorTargetView color_target, render::FrameSlot frame_slot,
        render::RenderGraphTextureState color_initial_state,
        render::RenderGraphTextureState color_final_state,
        const scene::RenderFramePlan3D& shadow_plan,
        const scene::RenderFramePlan3D& scene_plan,
        const render::MeshResourceTable<render::Mesh>& meshes,
        const render::MaterialResourceTable<render::FrameUniformMaterialInstance<
            render::PbrMaterialUniforms>>& material_instances,
        const std::unordered_map<render::MaterialHandle, render::PbrMaterialFactors,
                                 render::MaterialHandleHash>& material_factors);
    void record_shadow_pass(const vulkan::CommandRecorder& recorder,
                            const scene::RenderFramePlan3D& shadow_plan,
                            const render::MeshResourceTable<render::Mesh>& meshes) const;
    void record_scene_pass(const vulkan::CommandRecorder& recorder,
                           render::ColorTargetView color_target,
                           const scene::RenderFramePlan3D& scene_plan,
                           render::FrameSlot frame_slot,
                           const render::MeshResourceTable<render::Mesh>& meshes,
                           const render::MaterialResourceTable<render::FrameUniformMaterialInstance<
                               render::PbrMaterialUniforms>>& material_instances,
                           const std::unordered_map<render::MaterialHandle,
                                                    render::PbrMaterialFactors,
                                                    render::MaterialHandleHash>& material_factors)
        const;

    [[nodiscard]] const render::ShadowMapPass3D& shadow_pass() const;
    [[nodiscard]] const render::FrameUniformMaterialInstance<render::PbrSceneUniforms>&
    scene_material() const;
    [[nodiscard]] const render::FrameUniformMaterialInstance<render::PbrSkyboxUniforms>&
    skybox_material() const;
    [[nodiscard]] const render::GraphicsPipelineResource& opaque_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& alpha_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& skybox_pipeline() const;
    [[nodiscard]] const vulkan::DepthAttachment& depth_attachment() const;

    PbrViewRenderer3DConfig config_;
    const render::GeneratedPbrEnvironment* environment_ = nullptr;
    render::RenderGraphFrameExecutor graph_executor_;
    std::optional<render::ShadowMapPass3D> shadow_pass_;
    std::optional<render::FrameUniformMaterialInstance<render::PbrSceneUniforms>>
        scene_material_;
    std::optional<render::FrameUniformMaterialInstance<render::PbrSkyboxUniforms>>
        skybox_material_;
    std::optional<render::GraphicsPipelineResource> opaque_pipeline_;
    std::optional<render::GraphicsPipelineResource> alpha_pipeline_;
    std::optional<render::GraphicsPipelineResource> skybox_pipeline_;
    std::optional<vulkan::DepthAttachment> depth_attachment_;
    bool shadow_depth_is_sampled_ = false;
};

} // namespace cubey
