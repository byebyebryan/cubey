#pragma once

#include "textured_cube_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>

#ifndef CUBEY_TEXTURED_CUBE_SHADER_DIR
#error "CUBEY_TEXTURED_CUBE_SHADER_DIR must be defined by the textured_cube CMake target"
#endif

namespace cubey::examples::textured_cube {

inline constexpr std::uint32_t kCubeTriangleCount = 12;
inline constexpr float kCameraDistance = 4.2F;

struct SceneUniforms {
    cubey::math::Mat4 mvp;
    cubey::math::Mat4 model;
    std::array<float, 4> light_direction;
    std::array<float, 4> light_color;
    std::array<float, 4> ambient_color;
};

static_assert(sizeof(cubey::math::Mat4) == sizeof(float) * 16U);
static_assert(sizeof(SceneUniforms) == (sizeof(cubey::math::Mat4) * 2U) + (sizeof(float) * 12U));

class TexturedCubeApp {
  public:
    explicit TexturedCubeApp(RunConfig config);

    TexturedCubeApp(const TexturedCubeApp&) = delete;
    TexturedCubeApp& operator=(const TexturedCubeApp&) = delete;

    int run();

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context);
    void create_swapchain_resources(cubey::host::WindowedAppContext& context);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    void create_forward_pass(cubey::host::WindowedAppContext& context);
    void create_texture_resources(cubey::host::WindowedAppContext& context);
    void destroy_render_handles();
    [[nodiscard]] const cubey::render::Texture2D& texture() const;
    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<SceneUniforms>&
    material() const;
    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const;

    void create_scene();
    void update_scene_transform(const FrameTiming& timing);
    [[nodiscard]] cubey::Scene& scene();
    void destroy_scene_if_needed();

    [[nodiscard]] SceneUniforms
    current_scene_uniforms(const cubey::scene::RenderFramePlan3D& plan,
                           const cubey::scene::RenderDrawPacket3D& packet) const;
    [[nodiscard]] cubey::LightPacket3D
    current_light_packet(const cubey::scene::RenderFramePlan3D& plan) const;
    void update_scene_uniforms(const cubey::scene::RenderFramePlan3D& plan,
                               const cubey::scene::RenderDrawPacket3D& packet,
                               cubey::render::FrameSlot frame_slot);
    [[nodiscard]] cubey::scene::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const;
    void record_cube_frame(const cubey::host::WindowedRenderFrame& frame);

    RunConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity camera_entity_;
    cubey::Entity light_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MaterialHandle cube_material_handle_{};
    OrbitController orbit_controller_;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::optional<cubey::render::Texture2D> texture_;
    std::optional<cubey::render::FrameUniformMaterialInstance<SceneUniforms>> material_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace cubey::examples::textured_cube
