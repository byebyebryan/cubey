#pragma once

#include <cubey/core/math.h>
#include <cubey/render/deformation.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/shadow_map.h>
#include <cubey/render/target.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace cubey {

struct ForwardPbrRenderer3DConfig {
    std::filesystem::path pbr_vertex_shader{};
    std::filesystem::path pbr_fragment_shader{};
    std::filesystem::path skybox_vertex_shader{};
    std::filesystem::path skybox_fragment_shader{};
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

struct ForwardPbrRenderer3DTargetResourcesInfo {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkDescriptorSetLayout material_descriptor_set_layout = VK_NULL_HANDLE;
};

struct ForwardPbrRenderer3DSceneUniformInfo {
    math::Mat4 view_projection{1.0F};
    math::Mat4 light_view_projection{1.0F};
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    LightPacket3D light{};
    scene::Environment3D environment{};
    float environment_intensity = 1.0F;
    std::uint32_t prefiltered_mip_levels = 1;
    float environment_rotation_degrees = 0.0F;
};

struct ForwardPbrRenderer3DSkyboxUniformInfo {
    math::Mat4 view_projection{1.0F};
    math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    float environment_intensity = 1.0F;
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
};

struct ForwardPbrRenderer3DViewInfo {
    const SceneReadView* scene = nullptr;
    const scene::FrameRenderPlan3D* frame_plan = nullptr;
    Entity camera_entity{};
    Entity light_entity{};
    LightPacket3D fallback_light{};
};

struct ForwardPbrRenderer3DResourceInfo {
    const render::MeshResourceTable<render::Mesh>* meshes = nullptr;
    const render::FrameMeshResourceTable* frame_meshes = nullptr;
    std::span<const render::GpuDeformationCommand> deformation_commands{};
    const render::PbrMaterialTable* materials = nullptr;
};

struct ForwardPbrRenderer3DSettings {
    float environment_rotation_degrees = 0.0F;
    float exposure = 0.0F;
    render::PbrTonemap tonemap = render::PbrTonemap::Aces;
};

struct ForwardPbrRenderer3DRenderRequest {
    ForwardPbrRenderer3DTargetInfo target{};
    ForwardPbrRenderer3DViewInfo view{};
    ForwardPbrRenderer3DResourceInfo resources{};
    ForwardPbrRenderer3DSettings settings{};
};

struct ForwardPbrRenderer3DFramePlans {
    const scene::RenderFramePlan3D* shadow = nullptr;
    const scene::RenderFramePlan3D* scene = nullptr;
};

void validate_forward_pbr_renderer_3d_config(const ForwardPbrRenderer3DConfig& config);
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

    ForwardPbrRenderer3D(const ForwardPbrRenderer3D&) = delete;
    ForwardPbrRenderer3D& operator=(const ForwardPbrRenderer3D&) = delete;
    ForwardPbrRenderer3D(ForwardPbrRenderer3D&&) = delete;
    ForwardPbrRenderer3D& operator=(ForwardPbrRenderer3D&&) = delete;

    void create_global_resources(const vulkan::Device& device,
                                 const render::GeneratedPbrEnvironment& environment,
                                 std::uint32_t frame_slot_count);
    void create_swapchain_resources(const vulkan::Device& device,
                                    const ForwardPbrRenderer3DTargetResourcesInfo& info);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    void record(const ForwardPbrRenderer3DRenderRequest& request);

    [[nodiscard]] const render::GeneratedPbrEnvironment& environment() const;

  private:
    struct CompiledGraph {
        render::CompiledRenderGraph graph;
        render::RenderGraphTextureHandle scene_color;
    };
    enum class ForwardPbrPipelineVariant : std::uint8_t {
        Opaque,
        OpaqueDoubleSided,
        Alpha,
        AlphaDoubleSided,
        MaskShadow,
        MaskShadowDoubleSided,
        ShadowDoubleSided,
        Count,
    };

    [[nodiscard]] CompiledGraph
    current_render_graph(render::ColorTargetView color_target, render::FrameSlot frame_slot,
                         render::RenderGraphTextureState color_initial_state,
                         render::RenderGraphTextureState color_final_state,
                         const scene::RenderFramePlan3D& shadow_plan,
                         const scene::RenderFramePlan3D& scene_plan,
                         const render::MeshResourceTable<render::Mesh>& meshes,
                         const render::FrameMeshResourceTable* frame_meshes,
                         std::span<const render::GpuDeformationCommand> deformation_commands,
                         const render::PbrMaterialTable& materials);
    void record_shadow_pass(const vulkan::CommandRecorder& recorder,
                            const scene::RenderFramePlan3D& shadow_plan,
                            render::FrameSlot frame_slot, const render::MeshResolver& mesh_resolver,
                            const render::PbrMaterialTable& materials) const;
    void record_scene_pass(const vulkan::CommandRecorder& recorder,
                           render::ColorTargetView color_target,
                           const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
                           const render::MeshResolver& mesh_resolver,
                           const render::PbrMaterialTable& materials) const;
    void record_post_pass(const vulkan::CommandRecorder& recorder,
                          render::ColorTargetView color_target, render::FrameSlot frame_slot) const;
    void update_post_descriptor(const vulkan::Device& device, render::FrameSlot frame_slot,
                                const render::CompiledRenderGraph& graph,
                                const render::RenderGraphResourceSet& resources,
                                render::RenderGraphTextureHandle scene_color) const;

    [[nodiscard]] const render::ShadowMapPass3D& shadow_pass() const;
    [[nodiscard]] const render::FrameUniformMaterialInstance<render::PbrSceneUniforms>&
    scene_material() const;
    [[nodiscard]] const render::FrameUniformMaterialInstance<render::PbrSkyboxUniforms>&
    skybox_material() const;
    [[nodiscard]] const render::FrameUniformMaterialInstance<render::PbrPostUniforms>&
    post_material() const;
    [[nodiscard]] const render::GraphicsPipelineResource& opaque_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& opaque_double_sided_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& alpha_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& alpha_double_sided_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& mask_shadow_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& mask_shadow_double_sided_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& shadow_double_sided_pipeline() const;
    [[nodiscard]] std::optional<render::GraphicsPipelineResource>&
    pipeline_variant_slot(ForwardPbrPipelineVariant variant);
    [[nodiscard]] const render::GraphicsPipelineResource&
    pipeline_variant(ForwardPbrPipelineVariant variant) const;
    [[nodiscard]] const render::GraphicsPipelineResource& skybox_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& post_pipeline() const;
    [[nodiscard]] const vulkan::Sampler& post_sampler() const;
    [[nodiscard]] const vulkan::DepthAttachment& depth_attachment() const;

    ForwardPbrRenderer3DConfig config_;
    const render::GeneratedPbrEnvironment* environment_ = nullptr;
    render::RenderGraphFrameExecutor graph_executor_;
    std::optional<render::ShadowMapPass3D> shadow_pass_;
    std::optional<render::FrameUniformMaterialInstance<render::PbrSceneUniforms>> scene_material_;
    std::optional<render::FrameUniformMaterialInstance<render::PbrSkyboxUniforms>> skybox_material_;
    std::optional<render::FrameUniformMaterialInstance<render::PbrPostUniforms>> post_material_;
    std::array<std::optional<render::GraphicsPipelineResource>,
               static_cast<std::size_t>(ForwardPbrPipelineVariant::Count)>
        pipeline_variants_{};
    std::optional<render::GraphicsPipelineResource> skybox_pipeline_;
    std::optional<render::GraphicsPipelineResource> post_pipeline_;
    std::optional<vulkan::Sampler> post_sampler_;
    std::optional<vulkan::DepthAttachment> depth_attachment_;
    bool shadow_depth_is_sampled_ = false;
};

} // namespace cubey
