#pragma once
#include <cubey/render/terrain_surface_patch_scene.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <functional>
namespace cubey {
// Optional application composition for the narrow shared render stage: fixed
// sky, paired surfaces and one HDR display transform. Apps own this facade;
// TerrainSurfacePatchRenderer itself owns neither sky nor display composition.
class TerrainSurfacePatchScene {
  public:
    explicit TerrainSurfacePatchScene(std::filesystem::path shader_directory);
    ~TerrainSurfacePatchScene();
    bool ensure_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                          std::uint32_t slots, render::ColorTargetView target,
                          const render::TerrainSurfacePatchGrid& grid, std::span<const float> bed,
                          const render::TerrainSurfacePatchFields& fields,
                          bool integrated_terrain_diffuse = false,
                          const render::TerrainSurfacePatchSurface* surface = nullptr,
                          bool captured_daylight = false);
    void destroy_swapchain_resources();
    void destroy();
    [[nodiscard]] const render::GeneratedPbrEnvironment& fallback_environment() const;
    [[nodiscard]] std::vector<vulkan::GpuPassTiming> collect_timings(std::uint32_t slot);
    void record(const vulkan::Device& device, VkCommandBuffer commands,
                render::RenderGraphFrameExecutor& executor, render::FrameSlot slot,
                render::ColorTargetView target, bool present,
                const render::TerrainSurfacePatchFields& fields,
                const render::TerrainSurfacePatchGrid& grid,
                const render::TerrainSurfacePatchCamera& camera,
                render::TerrainSurfacePatchOptions options, double visual_clock_s,
                const render::TerrainSurfacePatchMaterial& material,
                const std::function<void(const vulkan::CommandRecorder&)>& display_overlay = {},
                bool profile = false, bool reset_visual_flow = false, unsigned terrain_view = 0U,
                unsigned water_view = 0U, unsigned surface_mode = 0U,
                const render::TerrainSurfacePatchEnvironment* environment = nullptr,
                render::TerrainSurfacePatchRain rain = {},
                std::span<const render::TerrainSurfacePatchWhitewaterSeed> whitewater_seeds = {},
                std::uint64_t whitewater_generation = 0U,
                const render::TerrainSurfaceContextDraw* context = nullptr);

  private:
    struct State;
    std::unique_ptr<State> state_;
    std::filesystem::path shader_directory_;
    [[nodiscard]] std::filesystem::path shader(const char* name) const;
};
} // namespace cubey
