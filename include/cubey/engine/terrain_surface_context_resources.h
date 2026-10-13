#pragma once
#include <cubey/render/terrain_surface_patch_scene.h>
#include <cubey/terrain/terrain_surface_context.h>
#include <memory>

namespace cubey {
// One immutable exterior-ground upload, shared by captured and live consumers.
// The app retires this owner after its submissions complete, before device exit.
class TerrainSurfaceContextResources {
  public:
    TerrainSurfaceContextResources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                   const terrain::TerrainSurfaceContextProduct& product);
    ~TerrainSurfaceContextResources();
    [[nodiscard]] render::TerrainSurfaceContextDraw draw() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace cubey
