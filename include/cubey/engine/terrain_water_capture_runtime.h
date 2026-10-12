#pragma once
#include <cubey/asset/terrain_water_capture.h>
#include <cubey/render/terrain_surface_patch_scene.h>
#include <memory>

namespace cubey {
// A retained captured patch, not a solver or a layer stitched into the radial
// terrain backdrop. Placement and source hashes are checked before GPU work.
class TerrainWaterCaptureRuntime {
  public:
    TerrainWaterCaptureRuntime(const std::filesystem::path& capture,
                               const std::filesystem::path& heightfield,
                               std::filesystem::path shaders);
    ~TerrainWaterCaptureRuntime();
    void ensure_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                          std::uint32_t slots, render::ColorTargetView target);
    void destroy_target_resources();
    void destroy();
    void record(const vulkan::Device& device, VkCommandBuffer commands, render::FrameSlot slot,
                render::ColorTargetView target, bool present,
                const render::TerrainSurfacePatchCamera& camera, double animation_s,
                bool water_visible, bool profile);
    [[nodiscard]] std::vector<vulkan::GpuPassTiming> collect_timings(std::uint32_t slot);
    // Expensive end-of-study check only, never in a measured frame window.
    void verify_immutable_fields(vulkan::GpuRuntime& gpu) const;
    [[nodiscard]] const asset::TerrainWaterCapture& capture() const;
    [[nodiscard]] std::string source_label() const;
    [[nodiscard]] VkDeviceSize retained_gpu_field_bytes() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace cubey
