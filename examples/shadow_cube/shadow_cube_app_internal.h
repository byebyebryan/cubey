#pragma once

#include "shadow_cube_app.h"

#include <cubey/engine/engine.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/shadow_map.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <cstdint>
#include <optional>

namespace cubey::examples::shadow_cube::detail {

inline constexpr float kShadowCubeGroundPlaneY = -1.5F;

class ShadowCubeApp {
  public:
    explicit ShadowCubeApp(RunConfig config);

    ShadowCubeApp(const ShadowCubeApp&) = delete;
    ShadowCubeApp& operator=(const ShadowCubeApp&) = delete;

    int run();

  private:
    struct ShadowRenderGraph {
        cubey::render::CompiledRenderGraph graph;
        cubey::render::RenderGraphTextureHandle scene_color;
    };

    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context);
    void create_swapchain_resources(cubey::host::WindowedAppContext& context);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    void create_scene();
    void create_shadow_resources(cubey::host::WindowedAppContext& context);
    void create_descriptors(cubey::host::WindowedAppContext& context);
    void create_present_resources(cubey::host::WindowedAppContext& context);
    void create_pipelines(cubey::host::WindowedAppContext& context);
    void create_scene_pipeline(cubey::host::WindowedAppContext& context);
    void create_present_pipeline(cubey::host::WindowedAppContext& context);

    void update_scene_transform(const cubey::FrameTiming& timing);
    [[nodiscard]] cubey::scene::FrameRenderPlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D color_extent) const;

    [[nodiscard]] ShadowRenderGraph
    current_render_graph(const cubey::host::WindowedRenderFrame& frame,
                         const cubey::scene::RenderFramePlan3D& shadow_plan,
                         const cubey::scene::RenderFramePlan3D& scene_plan) const;
    void update_present_descriptor(cubey::host::WindowedAppContext& context,
                                   cubey::render::FrameSlot frame_slot,
                                   const cubey::render::CompiledRenderGraph& graph,
                                   const cubey::render::RenderGraphResourceSet& resources,
                                   cubey::render::RenderGraphTextureHandle scene_color) const;
    void record_shadow_frame(cubey::host::WindowedAppContext& context,
                             const cubey::host::WindowedRenderFrame& frame);
    void record_shadow_pass(const cubey::vulkan::CommandRecorder& recorder,
                            const cubey::scene::RenderFramePlan3D& shadow_plan) const;
    void record_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                           cubey::render::ColorTargetView color_target,
                           const cubey::scene::RenderFramePlan3D& scene_plan,
                           const cubey::scene::RenderFramePlan3D& shadow_plan) const;
    void record_present_pass(const cubey::vulkan::CommandRecorder& recorder,
                             const cubey::host::WindowedRenderFrame& frame) const;

    [[nodiscard]] cubey::Scene& scene();
    void destroy_scene_if_needed();
    void destroy_render_handles();

    [[nodiscard]] const cubey::render::DepthTexture& shadow_depth() const;
    [[nodiscard]] const cubey::render::ShadowMapPass3D& shadow_pass() const;
    [[nodiscard]] const cubey::render::MaterialInstance& scene_material_instance() const;
    [[nodiscard]] const cubey::vulkan::Sampler& present_sampler() const;
    [[nodiscard]] const cubey::render::MaterialInstance& present_material_instance() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& shadow_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& scene_pipeline_resource() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& present_pipeline_resource() const;
    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const;

    RunConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity floor_entity_;
    cubey::Entity camera_entity_;
    cubey::Entity light_camera_entity_;
    cubey::Entity light_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MeshHandle floor_mesh_handle_{};
    cubey::render::MaterialHandle material_handle_{};
    cubey::OrbitController orbit_controller_;
    bool shadow_depth_is_sampled_ = false;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    std::optional<cubey::render::ShadowMapPass3D> shadow_pass_;
    std::optional<cubey::render::MaterialInstance> scene_material_instance_;
    std::optional<cubey::vulkan::Sampler> present_sampler_;
    std::optional<cubey::render::MaterialInstance> present_material_instance_;
    std::optional<cubey::render::GraphicsPipelineResource> scene_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> present_pipeline_resource_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
};

} // namespace cubey::examples::shadow_cube::detail
