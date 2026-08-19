#pragma once

#include "gltf_viewer_app.h"

#include <cubey/animation/gltf_animation.h>
#include <cubey/asset/gltf_asset.h>
#include <cubey/core/math.h>
#include <cubey/engine/atmosphere_background_atlas_runtime.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/engine/cloud_environment_runtime.h>
#include <cubey/engine/engine.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/engine/gltf_scene_importer.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/backdrop_surface_placement.h>
#include <cubey/render/cloud_layer.h>
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
#include <cubey/terrain/terrain_backdrop_preparation.h>
#include <cubey/vulkan/gpu_timestamps.h>

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
[[nodiscard]] cubey::Transform3D look_at_transform(cubey::math::Vec3 eye, cubey::math::Vec3 target);
[[nodiscard]] std::vector<cubey::render::PbrVertex> fallback_cube_vertices();
[[nodiscard]] std::vector<std::uint32_t> fallback_cube_indices();

class GltfViewerApp {
  public:
    explicit GltfViewerApp(GltfViewerProjectConfig config);

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
                                VkFormat color_format, std::uint32_t frame_slot_count);
    void destroy_swapchain_resources();
    void destroy_all_resources(cubey::vulkan::GpuRuntime& gpu);

    void create_imported_asset_scene(const cubey::vulkan::Device& device,
                                     cubey::vulkan::GpuRuntime& gpu,
                                     const cubey::asset::GltfAsset& asset,
                                     std::uint32_t frame_slot_count);
    [[nodiscard]] std::filesystem::path resolved_input_path() const;
    [[nodiscard]] std::filesystem::path resolved_environment_path() const;
    void create_default_textures(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu);
    void create_atmosphere_background_atlases(const cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu);
    void poll_atmosphere_background_atlases(const cubey::vulkan::Device& device,
                                            cubey::vulkan::GpuRuntime& gpu,
                                            const cubey::vulkan::FrameResources& frame_resources);
    void finish_atmosphere_background_atlases(const cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu);
    [[nodiscard]] cubey::render::AtmosphereBackgroundTextureBindings
    atmosphere_background_textures() const;
    [[nodiscard]] bool use_atmosphere_environment_source() const;
    [[nodiscard]] cubey::render::PbrEnvironmentTextureBindings pbr_environment_bindings() const;
    void create_atmosphere_environment_runtime(const cubey::vulkan::Device& device,
                                               std::uint32_t frame_slot_count);
    void create_cloud_environment_runtime(const cubey::vulkan::Device& device,
                                          cubey::vulkan::GpuRuntime& gpu,
                                          std::uint32_t frame_slot_count);
    void create_fallback_material(const cubey::vulkan::Device& device,
                                  std::uint32_t frame_slot_count);
    [[nodiscard]] std::vector<cubey::render::SampledImageMaterialBinding>
    fallback_material_sampled_images() const;
    void create_fallback_mesh(cubey::vulkan::GpuRuntime& gpu);
    void create_ibl_resources(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu);
    void create_terrain_backdrop_resources(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count);
    [[nodiscard]] bool terrain_backdrop_enabled() const noexcept;
    [[nodiscard]] bool ocean_backdrop_enabled() const noexcept;
    void update_ocean_environment_descriptors(const cubey::vulkan::Device& device,
                                              cubey::render::FrameSlot frame_slot);
    [[nodiscard]] cubey::ForwardPbrRenderer3DOceanSurface
    ocean_surface_frame(const cubey::SceneReadView& view, VkExtent2D color_extent);
    void collect_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                             cubey::render::FrameSlot frame_slot);
    [[nodiscard]] cubey::ForwardPbrRenderer3DTerrainBackdrop
    terrain_backdrop_frame(const cubey::SceneReadView& view,
                           const cubey::scene::FrameRenderPlan3D& frame_plan,
                           const cubey::render::AtmosphereEnvironmentFrameUniforms& atmosphere);

    void create_fallback_scene();
    void create_camera_and_light(cubey::SceneTransaction& setup);
    void update_animation(float delta_seconds);
    [[nodiscard]] bool update_atmosphere_time(double delta_seconds);
    void draw_ui(cubey::host::WindowedAppContext& context);
    void refresh_atmosphere_controls();
    void refresh_cloud_controls(cubey::host::WindowedAppContext& context);
    void refresh_atmosphere_lighting_scene();
    void update_camera_transform();
    [[nodiscard]] cubey::scene::FrameRenderPlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D color_extent) const;
    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_background_uniforms(const cubey::SceneReadView& view, VkExtent2D color_extent) const;
    [[nodiscard]] cubey::CloudEnvironmentConfig cloud_environment_config() const;
    [[nodiscard]] cubey::CloudEnvironmentRuntimeFrame
    cloud_environment_frame(const cubey::SceneReadView& view, VkExtent2D color_extent) const;
    [[nodiscard]] float display_exposure() const;
    [[nodiscard]] cubey::LightPacket3D fallback_light_packet() const;
    [[nodiscard]] cubey::Scene& scene();
    [[nodiscard]] const cubey::Scene& scene() const;
    void destroy_scene_if_needed();
    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const;
    [[nodiscard]] cubey::ForwardPbrRenderer3D& forward_pbr_renderer() const;

    void record_viewer_target(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                              cubey::render::ColorTargetView color_target,
                              cubey::render::FrameSlot frame_slot,
                              cubey::render::RenderGraphTextureState color_initial_state,
                              cubey::render::RenderGraphTextureState color_final_state,
                              cubey::render::RenderGraphCommandBufferMode command_buffer_mode);
    void record_atmosphere_environment_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                                 cubey::render::FrameSlot frame_slot);
    void record_cloud_environment_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                            cubey::render::FrameSlot frame_slot,
                                            const cubey::CloudEnvironmentRuntimeFrame& frame);
    void record_viewer_frame(cubey::host::WindowedAppContext& context,
                             const cubey::host::WindowedRenderFrame& frame);
    void record_viewer_capture(cubey::host::HeadlessPngContext& context,
                               const cubey::host::HeadlessCaptureFrame& frame,
                               VkCommandBuffer command_buffer,
                               const cubey::host::HeadlessRenderTarget& target);

    GltfViewerProjectConfig config_;
    cubey::Engine engine_;
    cubey::ForwardPbrRenderer3D* forward_pbr_renderer_ = nullptr;
    cubey::Scene* scene_ = nullptr;
    std::optional<cubey::asset::GltfAsset> asset_{};
    cubey::Entity camera_entity_{};
    cubey::Entity light_camera_entity_{};
    cubey::Entity light_entity_{};
    cubey::Bounds3D scene_bounds_{};
    cubey::OrbitController orbit_controller_;
    cubey::render::PbrDebugView debug_view_ = cubey::render::PbrDebugView::Final;
    cubey::AtmosphereEnvironmentRuntime atmosphere_runtime_{};
    cubey::AtmosphereEnvironmentRunState atmosphere_state_{};
    cubey::CloudEnvironmentConfig clouds_config_{};
    cubey::animation::GltfAnimationPlayback animation_playback_{};
    std::optional<cubey::animation::GltfAnimationSample> animation_sample_{};
    std::uint32_t triangle_count_ = 0;

    cubey::GltfSceneImportResources import_resources_{};
    cubey::GltfSceneImportResult import_result_{};
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
    cubey::AtmosphereBackgroundAtlasRuntime atmosphere_background_atlases_{};
    cubey::TerrainBackdropRuntime terrain_runtime_{};
    cubey::OceanSurfaceRuntime ocean_runtime_{};
    cubey::render::OceanSurfaceConfig ocean_config_{};
    std::optional<cubey::vulkan::GpuTimestampProfiler> gpu_profiler_{};
    float terrain_foreground_height_m_ = 200.0F;
    float terrain_minimum_foreground_height_m_ = 0.0F;
    cubey::render::BackdropSurfaceEnvelope terrain_surface_{};
    bool terrain_visible_ = true;
    bool terrain_shadows_ = true;
    bool terrain_reflections_ = true;
    cubey::render::TerrainBackdropMaterialMode terrain_material_ =
        cubey::render::TerrainBackdropMaterialMode::FilteredDetail;
    float ocean_foreground_height_m_ = 20.0F;
    float ocean_minimum_foreground_height_m_ = 0.0F;
    bool ocean_foreground_height_explicit_ = false;
    bool ocean_visible_ = true;
    double ocean_elapsed_seconds_ = 0.0;
    double ocean_delta_seconds_ = 1.0 / 60.0;
};

} // namespace cubey::projects::gltf_viewer
