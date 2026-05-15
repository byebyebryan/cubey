#pragma once

#include "gltf_viewer_app.h"

#include <cubey/animation/gltf_animation.h>
#include <cubey/asset/gltf_asset.h>
#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/engine/gltf_scene_importer.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace cubey::projects::gltf_viewer {

constexpr std::uint32_t kShadowMapSize = 2048;
constexpr std::uint32_t kFallbackCubeTriangleCount = 12;
extern const cubey::math::Vec3 kLightDirection;

[[nodiscard]] std::filesystem::path shader_path(const char* filename);
[[nodiscard]] std::filesystem::path bundled_sample_asset_path();
[[nodiscard]] std::filesystem::path bundled_sample_environment_path();
[[nodiscard]] cubey::ForwardPbrRenderer3DConfig forward_pbr_renderer_3d_config();
[[nodiscard]] cubey::render::RenderGraphTextureState undefined_texture_state();
[[nodiscard]] cubey::render::RenderGraphTextureState present_texture_state();
[[nodiscard]] cubey::render::RenderGraphTextureState color_attachment_texture_state();
[[nodiscard]] cubey::Transform3D look_at_transform(cubey::math::Vec3 eye, cubey::math::Vec3 target);
[[nodiscard]] std::vector<cubey::render::PbrVertex> fallback_cube_vertices();
[[nodiscard]] std::vector<std::uint32_t> fallback_cube_indices();

class GltfViewerApp {
  public:
    explicit GltfViewerApp(RunConfig config);

    GltfViewerApp(const GltfViewerApp&) = delete;
    GltfViewerApp& operator=(const GltfViewerApp&) = delete;

    int run();

  private:
    int run_windowed();
    int run_headless();

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count);
    void create_frame_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                VkFormat color_format);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    void create_imported_asset_scene(const cubey::vulkan::Device& device,
                                     cubey::vulkan::GpuRuntime& gpu,
                                     const cubey::asset::GltfAsset& asset,
                                     std::uint32_t frame_slot_count);
    [[nodiscard]] std::filesystem::path resolved_input_path() const;
    [[nodiscard]] std::filesystem::path resolved_environment_path() const;
    void create_default_textures(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu);
    void create_fallback_material(const cubey::vulkan::Device& device,
                                  std::uint32_t frame_slot_count);
    [[nodiscard]] std::vector<cubey::render::SampledImageMaterialBinding>
    fallback_material_sampled_images() const;
    void create_fallback_mesh(cubey::vulkan::GpuRuntime& gpu);
    void create_ibl_resources(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu);

    void create_fallback_scene();
    void create_camera_and_light(cubey::SceneTransaction& setup);
    void update_animation(float delta_seconds);
    void update_camera_transform();
    [[nodiscard]] cubey::scene::FrameRenderPlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D color_extent) const;
    [[nodiscard]] cubey::LightPacket3D fallback_light_packet() const;
    [[nodiscard]] cubey::Scene& scene();
    [[nodiscard]] const cubey::Scene& scene() const;
    void destroy_scene_if_needed();
    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const;
    [[nodiscard]] cubey::ForwardPbrRenderer3D& forward_pbr_renderer() const;
    [[nodiscard]] VkDescriptorSetLayout material_descriptor_set_layout() const;

    void record_viewer_target(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                              cubey::render::ColorTargetView color_target,
                              cubey::render::FrameSlot frame_slot,
                              cubey::render::RenderGraphTextureState color_initial_state,
                              cubey::render::RenderGraphTextureState color_final_state,
                              cubey::render::RenderGraphCommandBufferMode command_buffer_mode);
    void record_viewer_frame(cubey::host::WindowedAppContext& context,
                             const cubey::host::WindowedRenderFrame& frame);
    void record_viewer_capture(cubey::host::HeadlessPngContext& context,
                               VkCommandBuffer command_buffer,
                               const cubey::host::HeadlessRenderTarget& target);

    RunConfig config_;
    cubey::Engine engine_;
    cubey::ForwardPbrRenderer3D* forward_pbr_renderer_ = nullptr;
    cubey::Scene* scene_ = nullptr;
    std::optional<cubey::asset::GltfAsset> asset_{};
    cubey::Entity camera_entity_{};
    cubey::Entity light_camera_entity_{};
    cubey::Entity light_entity_{};
    cubey::Bounds3D scene_bounds_{};
    float camera_distance_ = 4.2F;
    cubey::OrbitController orbit_controller_;
    cubey::animation::GltfAnimationPlayback animation_playback_{};
    std::optional<cubey::animation::GltfAnimationSample> animation_sample_{};
    std::uint32_t triangle_count_ = 0;

    cubey::GltfSceneImportResources import_resources_{};
    cubey::GltfSceneImportResult import_result_{};
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
};

} // namespace cubey::projects::gltf_viewer
