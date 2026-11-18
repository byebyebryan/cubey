#pragma once

#include <cubey/core/math.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/render/deformation.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/hdr_color_pyramid.h>
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
#include <cubey/scene/scene.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace cubey {

constexpr float kForwardPbrRenderer3DDegreesToRadians = 0.017453292519943295769F;

struct ForwardPbrRenderer3DShadowPushConstants {
    math::Mat4 light_mvp{1.0F};
};

static_assert(sizeof(ForwardPbrRenderer3DShadowPushConstants) == sizeof(math::Mat4));

struct ForwardPbrTemporalRigidMotionUniforms {
    math::Mat4 current_jittered_view_projection{1.0F};
    math::Mat4 current_view_projection{1.0F};
    math::Mat4 previous_view_projection{1.0F};
    math::Vec4 extent{0.0F};
};

struct ForwardPbrTemporalRigidMotionPushConstants {
    math::Mat4 current_model{1.0F};
    math::Mat4 previous_model{0.0F};
};

static_assert(sizeof(ForwardPbrTemporalRigidMotionUniforms) == 208U);
static_assert(sizeof(ForwardPbrTemporalRigidMotionPushConstants) == 128U);

struct ForwardPbrTemporalRigidDrawKey {
    Entity entity{};
    render::MeshHandle mesh{};
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t instance_count = 1;
    std::uint32_t first_instance = 0;

    friend bool operator==(const ForwardPbrTemporalRigidDrawKey&,
                           const ForwardPbrTemporalRigidDrawKey&) = default;
};

struct ForwardPbrTemporalRigidDrawKeyHash {
    [[nodiscard]] std::size_t operator()(const ForwardPbrTemporalRigidDrawKey& key) const noexcept {
        std::size_t value = EntityHash{}(key.entity);
        const auto mix = [&value](std::size_t part) {
            value ^= part + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
        };
        mix(render::MeshHandleHash{}(key.mesh));
        mix(key.first_index);
        mix(static_cast<std::size_t>(static_cast<std::uint32_t>(key.vertex_offset)));
        mix(key.instance_count);
        mix(key.first_instance);
        return value;
    }
};

struct ForwardPbrTemporalRigidMotionDraw {
    ForwardPbrTemporalRigidDrawKey key{};
    math::Mat4 current_model{1.0F};
    math::Mat4 previous_model{0.0F};
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
};

[[nodiscard]] inline render::MaterialPassInfo
forward_pbr_temporal_rigid_motion_pass_info(const char* label, VkCullModeFlags cull_mode) {
    render::MaterialPassInfo info{.label = label};
    info.descriptor_sets.push_back({
        .set = 0,
        .bindings = {{.binding = 0,
                      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}},
    });
    info.push_constants.push_back(
        {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
         .offset = 0,
         .size = sizeof(ForwardPbrTemporalRigidMotionPushConstants)});
    info.cull_mode = cull_mode;
    info.depth_test = true;
    info.depth_write = false;
    info.depth_compare_op = VK_COMPARE_OP_EQUAL;
    return info;
}

[[nodiscard]] inline math::Vec3
forward_pbr_renderer_3d_camera_world_position(const SceneReadView& view, Entity camera) {
    const TransformInstance3D instance = view.transforms3d().instance(camera);
    const math::Mat4& world = view.transforms3d().world_affine_matrix(instance);
    return {world[3].x, world[3].y, world[3].z};
}

[[nodiscard]] inline float forward_pbr_renderer_3d_rotation_radians(float degrees) {
    return degrees * kForwardPbrRenderer3DDegreesToRadians;
}

[[nodiscard]] inline std::uint32_t forward_pbr_renderer_3d_binding(render::PbrSceneBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] inline std::uint32_t forward_pbr_renderer_3d_binding(render::PbrSkyboxBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] inline std::uint32_t forward_pbr_renderer_3d_binding(render::PbrPostBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::vector<render::SampledImageMaterialBinding>
forward_pbr_renderer_3d_scene_sampled_images(
    VkSampler shadow_sampler, VkImageView shadow_view,
    const render::PbrEnvironmentTextureBindings& environment);

void update_forward_pbr_renderer_3d_scene_environment_descriptors(
    const vulkan::Device& device, VkDescriptorSet scene_descriptor_set,
    const render::PbrEnvironmentFrameBindings& environment);

enum class ForwardPbrDrawRoute : std::uint8_t {
    ShadowOpaqueBack,
    ShadowOpaqueNoCull,
    ShadowMaskedBack,
    ShadowMaskedNoCull,
    SceneOpaqueBack,
    SceneOpaqueNoCull,
    SceneAlphaBack,
    SceneAlphaNoCull,
    TransmissionOpaqueBack,
    TransmissionOpaqueNoCull,
    TransmissionAlphaBack,
    TransmissionAlphaNoCull,
    Count,
};

// Renderer-private packet routing. Indices retain the source order within
// each route, so the recorder can bind route state without rescanning scenes.
class ForwardPbrDrawPlan {
  public:
    [[nodiscard]] std::span<const std::uint32_t> indices(ForwardPbrDrawRoute route) const;
    [[nodiscard]] bool has_transmission() const noexcept {
        return has_transmission_;
    }
    [[nodiscard]] bool opaque_layer_compatible() const {
        return !has_transmission_ && indices(ForwardPbrDrawRoute::SceneAlphaBack).empty() &&
               indices(ForwardPbrDrawRoute::SceneAlphaNoCull).empty();
    }

  private:
    friend ForwardPbrDrawPlan
    build_forward_pbr_draw_plan(const ForwardPbrRenderer3DFramePlans& frame_plans,
                                ForwardPbrRenderer3DFrameDrawMetrics* metrics);

    std::array<std::vector<std::uint32_t>, static_cast<std::size_t>(ForwardPbrDrawRoute::Count)>
        route_indices_{};
    bool has_transmission_ = false;
};

[[nodiscard]] ForwardPbrDrawPlan
build_forward_pbr_draw_plan(const ForwardPbrRenderer3DFramePlans& frame_plans,
                            ForwardPbrRenderer3DFrameDrawMetrics* metrics = nullptr);

struct ForwardPbrRenderer3D::Impl {
    explicit Impl(ForwardPbrRenderer3DConfig config);

    void create_global_resources(const vulkan::Device& device,
                                 const render::GeneratedPbrEnvironment& environment,
                                 std::uint32_t frame_slot_count);
    void create_global_resources(const vulkan::Device& device,
                                 const ForwardPbrRenderer3DGlobalResourcesInfo& info);
    void create_swapchain_resources(const vulkan::Device& device,
                                    const ForwardPbrRenderer3DTargetResourcesInfo& info);
    void update_environment_frame(const vulkan::Device& device, render::FrameSlot frame_slot,
                                  const render::PbrEnvironmentFrameBindings& environment);
    void update_atmosphere_background_texture_bindings(
        const vulkan::Device& device, const render::AtmosphereBackgroundTextureBindings& textures);
    void reset_swapchain_resources();
    void destroy_swapchain_resources();
    void destroy_all_resources();
    [[nodiscard]] ForwardPbrRenderer3DSceneTargetInfo scene_target_info() const;
    void record(const ForwardPbrRenderer3DRenderRequest& request);

    [[nodiscard]] bool has_global_resources() const;
    [[nodiscard]] bool has_swapchain_resources() const;
    [[nodiscard]] bool global_resources_ready() const;
    [[nodiscard]] bool swapchain_resources_ready() const;
    void require_global_resources() const;
    void require_swapchain_resources() const;
    void require_atmosphere_background_resources() const;
    void require_no_global_resources() const;
    void require_no_swapchain_resources() const;

    struct CompiledGraph {
        render::CompiledRenderGraph graph;
        render::RenderGraphTextureHandle scene_color;
        render::RenderGraphTextureHandle post_scene_color;
        render::RenderGraphTextureHandle scene_depth;
        render::RenderGraphTextureHandle cloud_scene_color;
        render::CloudLayerRuntimeFrame cloud;
        bool clouds_enabled = false;
        bool has_transmission = false;
        std::optional<render::TemporalFrame> temporal_frame{};
        render::RenderGraphTextureHandle temporal_motion{};
    };
    struct RecordedTargetState {
        bool shadow_depth_is_sampled = false;
        render::RenderGraphTextureState scene_depth_state =
            render::render_graph_undefined_texture_state();
    };
    enum class ForwardPbrTargetPipelineVariant : std::uint8_t {
        Opaque,
        OpaqueDoubleSided,
        Alpha,
        AlphaDoubleSided,
        MaskShadow,
        MaskShadowDoubleSided,
        Count,
    };

    [[nodiscard]] CompiledGraph
    current_render_graph(render::ColorTargetView color_target, render::FrameSlot frame_slot,
                         render::RenderGraphTextureState color_initial_state,
                         render::RenderGraphTextureState color_final_state,
                         const ForwardPbrRenderer3DFramePlans& frame_plans,
                         const render::MeshResourceTable<render::Mesh>& meshes,
                         const render::FrameMeshResourceTable* frame_meshes,
                         std::span<const render::GpuDeformationCommand> deformation_commands,
                         const render::PbrMaterialTable& materials, render::PbrDebugView debug_view,
                         ForwardPbrRenderer3DBackgroundMode background_mode,
                         const std::optional<ForwardPbrRenderer3DAtmosphereClouds>& clouds,
                         const std::optional<ForwardPbrRenderer3DTerrainBackdrop>& terrain,
                         const std::optional<ForwardPbrRenderer3DOceanSurface>& ocean,
                         const ForwardPbrDrawPlan& draw_plan, bool opaque_linear_output,
                         bool hybrid_transmission,
                         const std::optional<ForwardPbrRenderer3DSceneReadback>& readback,
                         const std::optional<render::TemporalFrame>& temporal_frame,
                         std::span<const ForwardPbrTemporalRigidMotionDraw> rigid_motion_draws);
    void record_shadow_pass(const vulkan::CommandRecorder& recorder,
                            const scene::RenderFramePlan3D& shadow_plan,
                            render::FrameSlot frame_slot, const render::MeshResolver& mesh_resolver,
                            const render::PbrMaterialTable& materials,
                            const ForwardPbrDrawPlan& draw_plan) const;
    void record_scene_pass(const vulkan::CommandRecorder& recorder,
                           render::ColorTargetView color_target,
                           const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
                           const render::MeshResolver& mesh_resolver,
                           const render::PbrMaterialTable& materials,
                           const ForwardPbrDrawPlan& draw_plan, render::PbrDebugView debug_view,
                           ForwardPbrRenderer3DBackgroundMode background_mode,
                           TerrainBackdropRuntime* terrain, OceanSurfaceRuntime* ocean,
                           bool preserve_scene_depth) const;
    void record_scene_opaque_pass(
        const vulkan::CommandRecorder& recorder, render::ColorTargetView color_target,
        const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
        const render::MeshResolver& mesh_resolver, const render::PbrMaterialTable& materials,
        const ForwardPbrDrawPlan& draw_plan, render::PbrDebugView debug_view,
        ForwardPbrRenderer3DBackgroundMode background_mode, TerrainBackdropRuntime* terrain,
        OceanSurfaceRuntime* ocean) const;
    void record_transmission_stage(const vulkan::CommandRecorder& recorder,
                                   render::ColorTargetView color_target,
                                   const scene::RenderFramePlan3D& scene_plan,
                                   render::FrameSlot frame_slot,
                                   const render::MeshResolver& mesh_resolver,
                                   const render::PbrMaterialTable& materials,
                                   const ForwardPbrDrawPlan& draw_plan) const;
    void record_scene_alpha_pass(const vulkan::CommandRecorder& recorder,
                                 render::ColorTargetView color_target,
                                 const scene::RenderFramePlan3D& scene_plan,
                                 render::FrameSlot frame_slot,
                                 const render::MeshResolver& mesh_resolver,
                                 const render::PbrMaterialTable& materials,
                                 const ForwardPbrDrawPlan& draw_plan) const;
    void record_post_pass(const vulkan::CommandRecorder& recorder,
                          render::ColorTargetView color_target, render::FrameSlot frame_slot) const;
    void record_rigid_motion_pass(const vulkan::CommandRecorder& recorder,
                                  render::ColorTargetView motion_target,
                                  render::DepthTargetView depth_target,
                                  render::FrameSlot frame_slot,
                                  const render::MeshResolver& mesh_resolver,
                                  std::span<const ForwardPbrTemporalRigidMotionDraw> draws) const;
    void ensure_temporal_resources(const vulkan::Device& device, VkExtent2D extent);
    [[nodiscard]] ForwardPbrRenderer3DTaaBypassReason
    temporal_bypass_reason(const ForwardPbrRenderer3DRenderRequest& request,
                           const scene::RenderFramePlan3D& scene_plan,
                           const ForwardPbrDrawPlan& draw_plan) const;
    [[nodiscard]] std::vector<ForwardPbrTemporalRigidMotionDraw>
    prepare_rigid_motion_draws(const scene::RenderFramePlan3D& scene_plan,
                               const ForwardPbrDrawPlan& draw_plan);
    void frame_submitted(vulkan::GpuSubmissionTicket ticket);
    void frame_discarded();
    void reset_temporal();
    void update_post_descriptor(const vulkan::Device& device, render::FrameSlot frame_slot,
                                const render::CompiledRenderGraph& graph,
                                const render::RenderGraphResourceSet& resources,
                                render::RenderGraphTextureHandle scene_color) const;

    [[nodiscard]] const render::ShadowMapPass3D& shadow_pass() const;
    [[nodiscard]] const render::FrameUniformMaterialInstance<render::PbrSceneUniforms>&
    scene_material() const;
    [[nodiscard]] const render::FrameUniformMaterialInstance<render::PbrSceneUniforms>&
    transmission_scene_material() const;
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
    target_pipeline_slot(ForwardPbrTargetPipelineVariant variant);
    [[nodiscard]] const render::GraphicsPipelineResource&
    target_pipeline(ForwardPbrTargetPipelineVariant variant) const;
    [[nodiscard]] const render::GraphicsPipelineResource& skybox_pipeline() const;
    [[nodiscard]] const render::GraphicsPipelineResource& post_pipeline() const;
    [[nodiscard]] const vulkan::Sampler& post_sampler() const;
    [[nodiscard]] const vulkan::DepthAttachment& depth_attachment() const;
    void ensure_refraction_pyramid(const vulkan::Device& device);
    [[nodiscard]] render::HdrColorPyramid& refraction_pyramid();
    [[nodiscard]] const render::HdrColorPyramid& refraction_pyramid() const;

    struct GlobalResources {
        render::PbrEnvironmentTextureBindings environment{};
        bool environment_initialized = false;
        render::RenderGraphFrameExecutor graph_executor{};
        std::optional<render::ShadowMapPass3D> shadow_pass{};
        std::optional<render::GraphicsPipelineResource> shadow_double_sided_pipeline{};
        std::optional<render::FrameUniformMaterialInstance<render::PbrSceneUniforms>>
            scene_material{};
        std::optional<render::FrameUniformMaterialInstance<render::PbrSkyboxUniforms>>
            skybox_material{};
        render::AtmosphereBackgroundFrame atmosphere_background{};
        std::optional<render::FrameUniformMaterialInstance<render::PbrPostUniforms>>
            post_material{};
    };

    struct SwapchainResources {
        std::array<std::optional<render::GraphicsPipelineResource>,
                   static_cast<std::size_t>(ForwardPbrTargetPipelineVariant::Count)>
            pipeline_variants{};
        std::optional<render::GraphicsPipelineResource> skybox_pipeline{};
        std::optional<render::GraphicsPipelineResource> post_pipeline{};
        std::optional<vulkan::Sampler> post_sampler{};
        std::optional<vulkan::DepthAttachment> depth_attachment{};
        std::optional<render::HdrColorPyramid> refraction_pyramid{};
        std::optional<render::FrameUniformMaterialInstance<render::PbrSceneUniforms>>
            transmission_scene_material{};
        std::optional<render::TemporalAntiAliasing> temporal_aa{};
        std::optional<render::FrameUniformMaterialInstance<ForwardPbrTemporalRigidMotionUniforms>>
            temporal_rigid_motion_material{};
        std::optional<render::GraphicsPipelineResource> temporal_rigid_motion_pipeline_back{};
        std::optional<render::GraphicsPipelineResource> temporal_rigid_motion_pipeline_no_cull{};
        VkExtent2D target_extent{};
        render::RenderGraphTextureState scene_depth_state =
            render::render_graph_undefined_texture_state();
        bool shadow_depth_is_sampled = false;
    };

    ForwardPbrRenderer3DConfig config_;
    GlobalResources global_{};
    SwapchainResources swapchain_{};
    ForwardPbrRenderer3DTemporalStatus temporal_status_{};
    std::optional<render::TemporalFrame> pending_temporal_frame_{};
    std::optional<RecordedTargetState> recorded_target_state_{};
    std::unordered_map<ForwardPbrTemporalRigidDrawKey, math::Mat4,
                       ForwardPbrTemporalRigidDrawKeyHash>
        accepted_rigid_models_{};
    std::unordered_map<ForwardPbrTemporalRigidDrawKey, math::Mat4,
                       ForwardPbrTemporalRigidDrawKeyHash>
        pending_rigid_models_{};
    bool force_temporal_reset_ = false;
    bool accepted_temporal_frame_ = false;
};

} // namespace cubey
