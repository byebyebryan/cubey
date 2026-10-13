#pragma once
#include <cubey/asset/terrain_site.h>
#include <cubey/asset/terrain_water_capture.h>
#include <cubey/render/terrain_surface_patch_scene.h>
#include <cubey/terrain/terrain_surface_context.h>
#include <cubey/terrain/terrain_surface_patch_lod.h>
#include <memory>

namespace cubey {
// A retained captured patch, not a solver or a layer stitched into the radial
// terrain backdrop. Placement and source hashes are checked before GPU work.
class TerrainWaterCaptureRuntime {
  public:
    TerrainWaterCaptureRuntime(const std::filesystem::path& capture,
                               const std::filesystem::path& heightfield,
                               std::filesystem::path shaders);
    // Reuse the surface renderer with sampled bed and zero water, not a fake
    // recorded simulation. The site/world package owns terrain coverage.
    TerrainWaterCaptureRuntime(const asset::TerrainSite& site, std::filesystem::path shaders);
    TerrainWaterCaptureRuntime(const asset::TerrainSite& site, const std::filesystem::path& capture,
                               std::filesystem::path shaders);
    ~TerrainWaterCaptureRuntime();
    void ensure_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                          std::uint32_t slots, render::ColorTargetView target);
    void destroy_target_resources();
    void destroy();
    void prepare_context(const std::filesystem::path& extension = {},
                         float maximum_spacing_m = 0.0F);
    void set_terrain_readability(unsigned mode);
    void prepare_fixed_lod(math::Vec2 focus_world, float fine_span_m = 16368.0F,
                           std::uint32_t maximum_vertices = 2000000U);
    [[nodiscard]] const terrain::TerrainSurfaceContextProduct* context_product() const;
    void record(const vulkan::Device& device, VkCommandBuffer commands, render::FrameSlot slot,
                render::ColorTargetView target, bool present,
                const render::TerrainSurfacePatchCamera& camera, double animation_s,
                bool water_visible, bool profile, bool context_visible = false,
                unsigned ground_view = 0U, bool fixed_lod = false);
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
