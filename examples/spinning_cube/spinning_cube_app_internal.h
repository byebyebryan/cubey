#pragma once

#include "spinning_cube_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/windowed_app.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>

#include <vulkan/vulkan.h>

#include <chrono>
#include <optional>

#ifndef CUBEY_SPINNING_CUBE_SHADER_DIR
#error "CUBEY_SPINNING_CUBE_SHADER_DIR must be defined by the spinning_cube CMake target"
#endif

namespace cubey::examples::spinning_cube {

class SpinningCubeApp {
  public:
    explicit SpinningCubeApp(SpinningCubeConfig config);

    SpinningCubeApp(const SpinningCubeApp&) = delete;
    SpinningCubeApp& operator=(const SpinningCubeApp&) = delete;

    int run();

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context);
    void create_swapchain_resources(cubey::host::WindowedAppContext& context);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    void create_forward_pass(cubey::host::WindowedAppContext& context);

    void create_scene();
    void update_scene_transform();
    [[nodiscard]] cubey::Scene& scene();
    void destroy_scene_if_needed();

    [[nodiscard]] cubey::scene::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const;
    void record_cube_frame(const cubey::host::WindowedRenderFrame& frame);
    void destroy_render_handles();
    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const;

    SpinningCubeConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity camera_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MaterialHandle cube_material_handle_{};
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace cubey::examples::spinning_cube
