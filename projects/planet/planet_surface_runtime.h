#pragma once

#include "planet_config.h"
#include "planet_frame.h"
#include "planet_surface.h"

#include <cubey/core/math.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/instance_buffer.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace cubey::projects::planet {

class PlanetSurfaceRuntime {
  public:
    void rebuild(const PlanetConfig& config, const PlanetFrame& frame, PlanetSurfaceView view);
    [[nodiscard]] bool plan_changed(const PlanetConfig& config, const PlanetFrame& frame,
                                    PlanetSurfaceView view) const;

    void clear_gpu_buffers();
    void clear_selection_history();
    void resize_frame_slots(std::uint32_t frame_slot_count);

    [[nodiscard]] const cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>&
    ensure_instance_buffer(cubey::vulkan::GpuRuntime& gpu, cubey::render::FrameSlot frame_slot);

    [[nodiscard]] const PlanetPatchGridMeshData& patch_grid() const;
    [[nodiscard]] const PlanetSurfaceDiagnostics& diagnostics() const;
    [[nodiscard]] cubey::math::DVec3 render_origin_world_m() const;
    [[nodiscard]] std::uint32_t instance_count() const;

  private:
    struct InstanceBufferSlot {
        std::optional<cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>> buffer{};
        std::uint64_t generation = 0;
    };

    void refresh_previous_selected_patch_ids(const PlanetSurfacePatchPlan& plan);
    void refresh_render_diagnostics(const PlanetConfig& config,
                                    const PlanetSurfacePatchPlan& plan);
    void mark_instance_buffers_stale();

    PlanetSurfaceBuildResult build_{};
    PlanetPatchGridMeshData patch_grid_{};
    std::vector<PlanetSurfaceGpuPatchInstance> patch_instances_{};
    std::vector<PlanetSurfacePatchId> previous_selected_patch_ids_{};
    cubey::math::DVec3 build_render_origin_world_m_{0.0, 0.0, 0.0};
    PlanetSurfaceView build_view_{};
    std::vector<InstanceBufferSlot> instance_buffers_{};
    std::uint64_t instance_generation_ = 0;
    bool has_build_ = false;
};

} // namespace cubey::projects::planet
