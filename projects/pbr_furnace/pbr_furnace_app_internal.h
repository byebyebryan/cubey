#pragma once

#include "pbr_furnace_app.h"
#include "pbr_furnace_scene.h"

#include <cubey/engine/engine.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace cubey::projects::pbr_furnace {

constexpr float kSphereRadius = 0.46F;
constexpr float kCameraDistance = 9.0F;
constexpr std::uint32_t kIblPrefilteredExtent = 64;
constexpr std::uint32_t kIblPrefilteredMipLevels = 5;
constexpr VkFormat kIblFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

struct WhitePbrEnvironment {
    cubey::render::TextureCube irradiance_cube;
    cubey::render::TextureCube prefiltered_cube;
    cubey::render::Texture2D brdf_lut;
    std::uint32_t prefiltered_mip_levels = 1;
    float intensity = 1.0F;
};

[[nodiscard]] std::filesystem::path shader_path(const char* filename);
[[nodiscard]] WhitePbrEnvironment create_white_pbr_environment(const cubey::vulkan::Device& device,
                                                               cubey::vulkan::GpuRuntime& gpu);
[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> make_pbr_sphere_mesh();

class PbrFurnaceApp {
  public:
    explicit PbrFurnaceApp(PbrFurnaceConfig config);

    PbrFurnaceApp(const PbrFurnaceApp&) = delete;
    PbrFurnaceApp& operator=(const PbrFurnaceApp&) = delete;

    int run();

  private:
    int run_windowed();
    int run_headless();

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count);
    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format);
    void destroy_swapchain_resources();
    void destroy_all_resources();

    void create_default_textures(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu);
    void create_scene_material(const cubey::vulkan::Device& device, std::uint32_t frame_slot_count);
    void create_materials(const cubey::vulkan::Device& device, std::uint32_t frame_slot_count);
    [[nodiscard]] std::vector<cubey::render::SampledImageMaterialBinding>
    material_sampled_images() const;
    void create_mesh(cubey::vulkan::GpuRuntime& gpu);
    void destroy_material_resources();

    void create_scene();
    void update_camera_transform();
    [[nodiscard]] cubey::scene::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const;
    [[nodiscard]] cubey::render::PbrSceneUniforms
    scene_uniforms(const cubey::SceneReadView& scene_view,
                   const cubey::scene::RenderFramePlan3D& plan, VkFormat color_format) const;
    [[nodiscard]] cubey::math::Vec3 camera_world_position(const cubey::SceneReadView& view) const;
    [[nodiscard]] cubey::Scene& scene();
    void destroy_scene_if_needed();

    void record_furnace_frame(VkCommandBuffer command_buffer,
                              cubey::render::ColorTargetView color_target,
                              cubey::render::FrameSlot frame_slot, bool present);

    [[nodiscard]] const cubey::render::Texture2D& dummy_shadow() const;
    [[nodiscard]] const WhitePbrEnvironment& white_environment() const;
    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<
        cubey::render::PbrSceneUniforms>&
    scene_material() const;
    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const;

    PbrFurnaceConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity camera_entity_{};
    cubey::OrbitController orbit_controller_;
    cubey::render::MeshHandle sphere_mesh_handle_{};

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    cubey::render::PbrMaterialTable materials_;
    std::vector<cubey::render::MaterialHandle> material_handles_;
    std::optional<cubey::render::PbrDefaultTextureSet> default_textures_;
    std::optional<WhitePbrEnvironment> white_environment_;
    std::optional<cubey::render::FrameUniformMaterialInstance<cubey::render::PbrSceneUniforms>>
        scene_material_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace cubey::projects::pbr_furnace
