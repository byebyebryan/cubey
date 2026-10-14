#pragma once

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/render/terrain_surface_patch_scene.h>

namespace cubey {

// One application-owned day/night controller, shared by Fluid and saved Terrain.
// No hydraulic state or solver clock is accepted by this interface.
struct TerrainSurfaceEnvironmentSettings {
    AtmosphereEnvironmentOptions atmosphere{};
};
struct TerrainSurfaceWeatherFrame {
    render::TerrainSurfacePatchEnvironment environment{};
    const render::AtmosphereBackgroundFrame* background = nullptr;
    double elapsed_s = 0.0, time_hours = 0.0;
    std::uint64_t sky_generation = 0U;
};
[[nodiscard]] render::ViewRayBasis3D
terrain_surface_environment_view_rays(const render::TerrainSurfacePatchCamera& camera);

class TerrainSurfaceEnvironment {
  public:
    explicit TerrainSurfaceEnvironment(TerrainSurfaceEnvironmentSettings settings = {});
    ~TerrainSurfaceEnvironment();
    TerrainSurfaceEnvironment(const TerrainSurfaceEnvironment&) = delete;
    TerrainSurfaceEnvironment& operator=(const TerrainSurfaceEnvironment&) = delete;
    bool ensure_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                          std::uint32_t slots, render::ColorTargetView target,
                          const std::filesystem::path& shaders);
    void advance(double wall_delta_s);
    [[nodiscard]] TerrainSurfaceWeatherFrame frame(const vulkan::Device& device,
                                                   const vulkan::CommandRecorder& recorder,
                                                   render::FrameSlot slot,
                                                   const render::TerrainSurfacePatchCamera& camera,
                                                   const render::GeneratedPbrEnvironment& fallback);
    void destroy();
    [[nodiscard]] AtmosphereEnvironmentRunState& atmosphere() noexcept;
    [[nodiscard]] double elapsed_seconds() const noexcept;
    void select_preset(std::string_view preset);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace cubey
