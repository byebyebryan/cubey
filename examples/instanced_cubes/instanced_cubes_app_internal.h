#pragma once

#include "instanced_cubes_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/instance_buffer.h>
#include <cubey/render/mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/view_3d.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <type_traits>

#ifndef CUBEY_INSTANCED_CUBES_SHADER_DIR
#error "CUBEY_INSTANCED_CUBES_SHADER_DIR must be defined by the instanced_cubes CMake target"
#endif

namespace cubey::examples::instanced_cubes {

inline constexpr std::uint32_t kGridColumns = 9;
inline constexpr std::uint32_t kGridRows = 5;
inline constexpr std::uint32_t kInstanceCount = kGridColumns * kGridRows;
inline constexpr std::uint32_t kCubeTriangleCount = 12;
inline constexpr float kGridSpacing = 1.28F;
inline constexpr float kCameraDistance = 10.2F;

struct CubeInstanceData {
    cubey::math::Mat4 model;
    cubey::math::Vec4 color;
};

static_assert(std::is_trivially_copyable_v<CubeInstanceData>);
static_assert(sizeof(CubeInstanceData) == sizeof(cubey::math::Mat4) + sizeof(cubey::math::Vec4));

class InstancedCubesApp {
  public:
    explicit InstancedCubesApp(InstancedCubesConfig config);

    InstancedCubesApp(const InstancedCubesApp&) = delete;
    InstancedCubesApp& operator=(const InstancedCubesApp&) = delete;

    int run();

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context);
    void create_swapchain_resources(cubey::host::WindowedAppContext& context);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    void create_forward_pass(cubey::host::WindowedAppContext& context);
    void destroy_render_handles();
    [[nodiscard]] const cubey::render::InstanceBuffer<CubeInstanceData>& instance_buffer() const;
    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const;

    void create_scene();
    void update_camera_transform();
    [[nodiscard]] cubey::Scene& scene();
    void destroy_scene_if_needed();

    [[nodiscard]] cubey::scene::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const;
    [[nodiscard]] cubey::math::Mat4 cube_spin_matrix(const FrameTiming& timing) const;
    void record_cube_frame(const cubey::host::WindowedRenderFrame& frame);

    InstancedCubesConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity camera_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MaterialHandle cube_material_handle_{};
    OrbitController orbit_controller_;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::optional<cubey::render::InstanceBuffer<CubeInstanceData>> instance_buffer_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace cubey::examples::instanced_cubes
