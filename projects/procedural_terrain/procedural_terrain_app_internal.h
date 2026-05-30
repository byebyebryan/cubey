#pragma once

#include "procedural_terrain_app.h"
#include "procedural_terrain_fields.h"
#include "procedural_terrain_mesh.h"
#include "procedural_terrain_ui.h"

#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/mesh.h>
#include <cubey/scene/camera_3d.h>

#include <vulkan/vulkan.h>

#include <filesystem>
#include <optional>
#include <string>

namespace cubey::projects::procedural_terrain {

struct TerrainPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_debug{0.35F, 0.75F, 0.45F, 0.0F};
    cubey::math::Vec4 field_ranges{0.0F, 1.0F, 1.0F, 1.0F};
};

static_assert(sizeof(TerrainPushConstants) ==
              sizeof(cubey::math::Mat4) + (sizeof(cubey::math::Vec4) * 2U));
static_assert(sizeof(TerrainPushConstants) <= 128U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename);

class ProceduralTerrainApp {
  public:
    explicit ProceduralTerrainApp(RunConfig config);

    ProceduralTerrainApp(const ProceduralTerrainApp&) = delete;
    ProceduralTerrainApp& operator=(const ProceduralTerrainApp&) = delete;

    int run();

  private:
    int run_windowed();
    int run_headless();

    void create_global_resources_if_needed(cubey::vulkan::GpuRuntime& gpu);
    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format);
    void destroy_swapchain_resources();
    void destroy_all_resources();
    void draw_ui(cubey::host::WindowedAppContext& context);
    void update_input(const cubey::host::WindowedAppContext& context, const FrameTiming& timing);
    void rebuild_terrain_resources(cubey::host::WindowedAppContext& context);
    void refresh_camera_limits_for_terrain();
    void refresh_diagnostics(double rebuild_ms);
    [[nodiscard]] std::optional<cubey::host::FrameStatsSample>
    record_frame_stats(VkExtent2D extent, const FrameTiming& timing);
    void record_terrain_frame(VkCommandBuffer command_buffer,
                              cubey::render::ColorTargetView color_target, bool present);

    [[nodiscard]] TerrainPushConstants push_constants(VkExtent2D extent) const;
    [[nodiscard]] const cubey::render::Mesh& mesh() const;
    [[nodiscard]] const cubey::render::Mesh& water_mesh() const;
    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const;

    RunConfig config_;
    TerrainConfig terrain_config_{};
    TerrainConfig edit_terrain_config_{};
    TerrainFieldData fields_{};
    TerrainMeshData mesh_data_{};
    TerrainMeshData water_mesh_data_{};
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_{cubey::Camera3DConfig{.near_z = 0.1F, .far_z = 5000.0F}};
    std::optional<cubey::render::Mesh> mesh_;
    std::optional<cubey::render::Mesh> water_mesh_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
    cubey::host::FrameStats ui_frame_stats_;
    TerrainDiagnostics diagnostics_{};
    std::optional<cubey::host::FrameStatsSnapshot> latest_frame_stats_;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    std::uint64_t rebuild_count_ = 0;
    std::string rebuild_error_{};
    bool water_visible_ = true;
    bool rebuild_requested_ = false;
    bool discard_edits_requested_ = false;
    bool reset_camera_requested_ = false;
};

} // namespace cubey::projects::procedural_terrain
