#pragma once

#include <cubey/core/math.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
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

namespace cubey {

constexpr float kForwardPbrRenderer3DDegreesToRadians = 0.017453292519943295769F;

struct ForwardPbrRenderer3DShadowPushConstants {
    math::Mat4 light_mvp{1.0F};
};

static_assert(sizeof(ForwardPbrRenderer3DShadowPushConstants) == sizeof(math::Mat4));

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

struct ForwardPbrRenderer3D::Impl {
    explicit Impl(ForwardPbrRenderer3DConfig config);

    void create_global_resources(const vulkan::Device& device,
                                 const render::GeneratedPbrEnvironment& environment,
                                 std::uint32_t frame_slot_count);
    void create_global_resources(const vulkan::Device& device,
                                 const ForwardPbrRenderer3DGlobalResourcesInfo& info);
    void create_swapchain_resources(const vulkan::Device& device,
                                    const ForwardPbrRenderer3DTargetResourcesInfo& info);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    void record(const ForwardPbrRenderer3DRenderRequest& request);

    [[nodiscard]] const render::GeneratedPbrEnvironment& environment() const;
    [[nodiscard]] bool has_global_resources() const;
    [[nodiscard]] bool has_swapchain_resources() const;
    void require_global_resources() const;
    void require_swapchain_resources() const;
    void require_atmosphere_background_resources() const;
    void require_no_global_resources() const;
    void require_no_swapchain_resources() const;

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

    [[nodiscard]] CompiledGraph current_render_graph(
        render::ColorTargetView color_target, render::FrameSlot frame_slot,
        render::RenderGraphTextureState color_initial_state,
        render::RenderGraphTextureState color_final_state,
        const scene::RenderFramePlan3D& shadow_plan, const scene::RenderFramePlan3D& scene_plan,
        const render::MeshResourceTable<render::Mesh>& meshes,
        const render::FrameMeshResourceTable* frame_meshes,
        std::span<const render::GpuDeformationCommand> deformation_commands,
        const render::PbrMaterialTable& materials, render::PbrDebugView debug_view,
        ForwardPbrRenderer3DBackgroundMode background_mode);
    void record_shadow_pass(const vulkan::CommandRecorder& recorder,
                            const scene::RenderFramePlan3D& shadow_plan,
                            render::FrameSlot frame_slot, const render::MeshResolver& mesh_resolver,
                            const render::PbrMaterialTable& materials) const;
    void record_scene_pass(const vulkan::CommandRecorder& recorder,
                           render::ColorTargetView color_target,
                           const scene::RenderFramePlan3D& scene_plan, render::FrameSlot frame_slot,
                           const render::MeshResolver& mesh_resolver,
                           const render::PbrMaterialTable& materials,
                           render::PbrDebugView debug_view,
                           ForwardPbrRenderer3DBackgroundMode background_mode) const;
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

    struct GlobalResources {
        const render::GeneratedPbrEnvironment* environment = nullptr;
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
                   static_cast<std::size_t>(ForwardPbrPipelineVariant::Count)>
            pipeline_variants{};
        std::optional<render::GraphicsPipelineResource> skybox_pipeline{};
        std::optional<render::GraphicsPipelineResource> post_pipeline{};
        std::optional<vulkan::Sampler> post_sampler{};
        std::optional<vulkan::DepthAttachment> depth_attachment{};
        bool shadow_depth_is_sampled = false;
    };

    ForwardPbrRenderer3DConfig config_;
    GlobalResources global_{};
    SwapchainResources swapchain_{};
};

} // namespace cubey
