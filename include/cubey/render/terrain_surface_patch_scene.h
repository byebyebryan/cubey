#pragma once

#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/render_graph_frame.h>
#include <cubey/render/target.h>
#include <cubey/render/terrain_surface_patch.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <functional>
#include <optional>
#include <span>

namespace cubey::render {
[[nodiscard]] AtmosphereEnvironmentConfig terrain_surface_patch_fixed_daylight();
struct TerrainSurfacePatchEnvironment {
    PbrEnvironmentTextureBindings textures;
    AtmosphereEnvironmentLighting lighting;
    float exposure = 0.4F;
    VkSampler sky_radiance_sampler = VK_NULL_HANDLE;
    VkImageView sky_radiance_view = VK_NULL_HANDLE;
    AtmosphereEnvironmentFrameUniforms atmosphere_frame{};
};
struct TerrainSurfacePatchGrid {
    std::uint32_t grid_width = 0U, grid_height = 0U;
    float cell_size_m = 1.0F, minimum_wet_depth_m = 0.0001F;
};
struct TerrainSurfacePatchCamera {
    math::Mat4 view_projection{1.0F};
    math::Vec3 position{};
};
// Borrowed handles only. The scene never uploads or writes these fields.
struct TerrainSurfacePatchFields {
    const vulkan::Buffer& bed;
    const vulkan::Buffer& depth;
    const vulkan::Buffer& velocity;
    const vulkan::Buffer& coverage;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};
struct TerrainSurfacePatchSurface {
    std::uint32_t width = 0U, height = 0U;
    std::span<const math::Vec4> landform, climate, correlated;
};
struct TerrainSurfacePatchOptions {
    std::optional<float> terrain_height_scale{};
    bool terrain_thin_water_composite = false;
    bool native_bilinear_water = false;
    bool native_bspline_surface = true;
    bool native_display_coverage = false;
    std::uint32_t native_surface_subdivision = 2U;
    bool native_shadow_scene_fit = true;
    std::uint32_t native_shadow_extent = 2048U;
    bool water_visible = true;
};
struct TerrainSurfacePatchRain {
    std::uint32_t streak_count = 0U;
    double clock_s = 0.0, motion_s = 0.0;
    std::optional<double> applied_mm_per_hour;
};
struct TerrainSurfacePatchWhitewaterSeed {
    std::uint32_t cell = 0U, slot = 0U;
    float weight = 1.0F, padding = 0.0F;
};

// Complete paired-patch scene: sky, matched shadow/ground/water, render-only
// motion, HDR display. Not a terrain backdrop, weather service or simulation.
class TerrainSurfacePatchScene {
  public:
    explicit TerrainSurfacePatchScene(std::filesystem::path shader_directory);
    ~TerrainSurfacePatchScene();
    bool ensure_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                          std::uint32_t slots, ColorTargetView target,
                          const TerrainSurfacePatchGrid& grid, std::span<const float> bed,
                          const TerrainSurfacePatchFields& fields,
                          bool integrated_terrain_diffuse = false,
                          const TerrainSurfacePatchSurface* surface = nullptr,
                          bool captured_daylight = false);
    void destroy_swapchain_resources();
    void destroy();
    [[nodiscard]] const GeneratedPbrEnvironment& fallback_environment() const;
    [[nodiscard]] std::vector<vulkan::GpuPassTiming> collect_timings(std::uint32_t slot);
    void record(const vulkan::Device& device, VkCommandBuffer commands,
                RenderGraphFrameExecutor& executor, FrameSlot slot, ColorTargetView target,
                bool present, const TerrainSurfacePatchFields& fields,
                const TerrainSurfacePatchGrid& grid, const TerrainSurfacePatchCamera& camera,
                TerrainSurfacePatchOptions options, double visual_clock_s,
                const TerrainSurfacePatchMaterial& material,
                const std::function<void(const vulkan::CommandRecorder&)>& display_overlay = {},
                bool profile = false, bool reset_visual_flow = false, unsigned terrain_view = 0U,
                unsigned water_view = 0U, unsigned surface_mode = 0U,
                const TerrainSurfacePatchEnvironment* environment = nullptr,
                TerrainSurfacePatchRain rain = {},
                std::span<const TerrainSurfacePatchWhitewaterSeed> whitewater_seeds = {},
                std::uint64_t whitewater_generation = 0U);

  private:
    struct State;
    std::unique_ptr<State> state_;
    std::filesystem::path shader_directory_;
    [[nodiscard]] std::filesystem::path shader(const char* name) const;
};
} // namespace cubey::render
