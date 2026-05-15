#pragma once

#include "material_cubes_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
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

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace cubey::examples::material_cubes::detail {

inline constexpr std::uint32_t kMaterialGridColumns = 7;
inline constexpr std::uint32_t kMaterialGridRows = 5;
inline constexpr std::uint32_t kMaterialCubeCount = kMaterialGridColumns * kMaterialGridRows;
inline constexpr std::uint32_t kCubeTriangleCount = 12;
inline constexpr std::uint32_t kShadowMapSize = 2048;
inline constexpr float kCameraDistance = 8.8F;
inline constexpr float kMaterialGridSpacingX = 1.16F;
inline constexpr float kMaterialGridSpacingY = 1.12F;
inline constexpr cubey::math::Vec3 kMaterialCubeScale{0.42F, 0.42F, 0.42F};
inline constexpr cubey::math::Vec4 kNeutralMaterialBaseColor{0.56F, 0.55F, 0.52F, 1.0F};
inline constexpr float kMinimumRoughness = 0.04F;

struct MaterialCube {
    cubey::Entity entity{};
    cubey::render::MaterialHandle material{};
};

class MaterialCubesApp {
  public:
    explicit MaterialCubesApp(RunConfig config);

    MaterialCubesApp(const MaterialCubesApp&) = delete;
    MaterialCubesApp& operator=(const MaterialCubesApp&) = delete;

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
    void create_materials(const cubey::vulkan::Device& device, std::uint32_t frame_slot_count);
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

    void record_cube_frame(cubey::host::WindowedAppContext& context,
                           const cubey::host::WindowedRenderFrame& frame);

    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const;
    [[nodiscard]] cubey::ForwardPbrRenderer3D& forward_pbr_renderer() const;

    RunConfig config_;
    cubey::Engine engine_;
    cubey::ForwardPbrRenderer3D* forward_pbr_renderer_ = nullptr;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity camera_entity_{};
    cubey::Entity light_camera_entity_{};
    cubey::Entity light_entity_{};
    OrbitController orbit_controller_;
    cubey::render::PbrDebugView debug_view_ = cubey::render::PbrDebugView::Final;
    cubey::render::MeshHandle cube_mesh_handle_{};

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    cubey::render::PbrMaterialTable materials_;
    std::vector<cubey::render::MaterialHandle> material_handles_;
    std::vector<MaterialCube> cubes_;
    std::optional<cubey::render::PbrDefaultTextureSet> default_textures_;
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
};

} // namespace cubey::examples::material_cubes::detail
