#pragma once

#include "fluid_25d_commands.h"
#include "fluid_25d_scenic_material.h"
#include "fluid_25d_terrain_surface.h"
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/generated_ibl.h>
#include <memory>

namespace cubey::projects::fluid::fluid_25d {

// Caller-owned lighting/probe contract. Scenic does not own a sky service.
struct Fluid25DScenicEnvironment {
    render::PbrEnvironmentTextureBindings textures;
    render::AtmosphereEnvironmentLighting lighting;
    float exposure = 0.4F;
};

// Native frontend only. All hydraulic buffers are read-only inputs; the
// legacy draw graph and shaders are deliberately separate from this path.
class Fluid25DScenic {
  public:
    Fluid25DScenic();
    ~Fluid25DScenic();
    // Returns true only after potentially blocking local resource creation.
    bool ensure_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots,
                          render::ColorTargetView target, const Fluid25DConfig& config,
                          const Fluid25DScenarioData& scenario, const Fluid25DGpuResources& fields,
                          bool integrated_terrain_diffuse = false,
                          const Fluid25DTerrainSurface* surface = nullptr);
    void destroy_swapchain_resources();
    void destroy();
    [[nodiscard]] const render::GeneratedPbrEnvironment& fallback_environment() const;
    [[nodiscard]] std::vector<vulkan::GpuPassTiming> collect_timings(std::uint32_t slot);
    void record(vulkan::Device& device, VkCommandBuffer commands,
                render::RenderGraphFrameExecutor& executor, render::FrameSlot slot,
                render::ColorTargetView target, Fluid25DRenderTargetMode target_mode,
                const Fluid25DGpuResources& resources, const Fluid25DConfig& config,
                const Fluid25DRenderCamera& camera, Fluid25DCatchmentRenderOptions options,
                double visual_clock_s, const Fluid25DScenicMaterial& material,
                Fluid25DMotionMarkers* markers, float marker_fraction, bool profile = false,
                bool reset_visual_flow = false, unsigned terrain_view = 0U,
                unsigned water_view = 0U, unsigned surface_mode = 0U,
                const Fluid25DScenicEnvironment* environment = nullptr);

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace cubey::projects::fluid::fluid_25d
