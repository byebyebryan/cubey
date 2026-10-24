#pragma once

#include "pbr_materials_app.h"
#include "pbr_materials_presentation.h"

#include <cubey/asset/material_recipe.h>
#include <cubey/core/math.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/engine/engine.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/engine/material_recipe.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace cubey::projects::pbr_materials::detail {

inline constexpr std::uint32_t kMaterialGridColumns = 4;
inline constexpr std::uint32_t kSphereTriangleCount = 64 * 128 * 2;
inline constexpr std::uint32_t kShadowMapSize = 2048;
inline constexpr float kCameraDistance = 3.3F;
inline constexpr float kMaterialGridSpacingX = 0.62F;
inline constexpr float kMaterialGridSpacingY = .68F;
inline constexpr cubey::math::Vec3 kMaterialSpecimenScale{1.0F, 1.0F, 1.0F};
inline constexpr cubey::math::Vec4 kNeutralMaterialBaseColor{0.56F, 0.55F, 0.52F, 1.0F};
inline constexpr float kMinimumRoughness = 0.04F;

struct MaterialSpecimen {
    cubey::Entity entity{};
    cubey::render::MaterialHandle material{};
};

class PbrMaterialsApp {
  public:
    explicit PbrMaterialsApp(PbrMaterialsConfig config);

    PbrMaterialsApp(const PbrMaterialsApp&) = delete;
    PbrMaterialsApp& operator=(const PbrMaterialsApp&) = delete;

    int run();

  private:
    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count);
    void create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                    VkFormat color_format);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    void create_default_textures(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu);
    void create_materials();
    [[nodiscard]] std::vector<cubey::render::SampledImageMaterialBinding>
    material_sampled_images() const;
    void create_mesh(cubey::vulkan::GpuRuntime& gpu);
    void create_ibl_resources(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu);
    void destroy_material_resources();
    void destroy_render_handles();

    void create_scene();
    void update_scene_transform(const FrameTiming& timing);
    [[nodiscard]] cubey::scene::FrameRenderPlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D color_extent) const;
    [[nodiscard]] cubey::LightPacket3D fallback_light_packet() const;
    [[nodiscard]] cubey::Scene& scene();
    void destroy_scene_if_needed();

    int run_headless();
    void record_frame(const cubey::vulkan::Device&, VkCommandBuffer, cubey::render::ColorTargetView,
                      cubey::render::FrameSlot, bool, cubey::profiling::ProfileRecorder*,
                      std::uint64_t);
    void record_workshop_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& frame);

    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const;
    [[nodiscard]] cubey::ForwardPbrRenderer3D& forward_pbr_renderer() const;

    PbrMaterialsConfig config_;
    cubey::Engine engine_;
    cubey::ForwardPbrRenderer3D* forward_pbr_renderer_ = nullptr;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity camera_entity_{};
    cubey::Entity light_camera_entity_{};
    cubey::Entity light_entity_{};
    OrbitController orbit_controller_;
    cubey::render::PbrDebugView debug_view_ = cubey::render::PbrDebugView::Final;
    cubey::render::MeshHandle sphere_mesh_handle_{};

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::vector<cubey::render::PbrMaterialTable> materials_;
    std::vector<cubey::asset::MaterialRecipe> recipes_, originals_;
    std::vector<cubey::asset::MaterialCatalogEntry> catalog_;
    std::string family_filter_ = "all";
    std::vector<std::uint64_t> published_revision_;
    std::vector<bool> stress_done_;
    std::uint64_t revision_ = 1;
    int selected_ = -1;
    cubey::render::MeshHandle block_mesh_handle_{};
    cubey::render::MeshHandle swatch_mesh_handle_{}, cut_block_mesh_handle_{};
    std::optional<vulkan::GpuTimestampProfiler> whole_profiler_, pass_profiler_;
    AtmosphereEnvironmentRuntime atmosphere_;
    std::optional<render::AtmosphereBackgroundPlaceholderTextures> atmosphere_textures_;
    render::CloudLayerRuntimeShaderFiles cloud_shaders_{};
    std::string requested_environment_;
    void switch_environment(const vulkan::Device&, vulkan::GpuRuntime&, VkExtent2D, VkFormat);
    void create_environment(const vulkan::Device&, vulkan::GpuRuntime&, std::uint32_t);
    void create_environment_targets(const vulkan::Device&, VkExtent2D, std::uint32_t);
    CloudEnvironmentRuntimeFrame cloud_frame(VkExtent2D) const;
    render::ViewRayBasis3D view_rays(VkExtent2D) const;
    std::string ui_error_;
    char recipe_path_[512] = "outputs/material-recipe.json";
    void draw_ui();
    void refresh_specimens();
    void leave_presentation();
    double presentation_seconds_ = 0;
    void publish_materials(cubey::render::FrameSlot slot);
    cubey::render::PbrMaterialDefinition material_definition(std::size_t index) const;
    std::vector<cubey::render::MaterialHandle> material_handles_;
    std::vector<MaterialSpecimen> specimens_;
    std::optional<cubey::render::PbrDefaultTextureSet> default_textures_;
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
};

} // namespace cubey::projects::pbr_materials::detail
