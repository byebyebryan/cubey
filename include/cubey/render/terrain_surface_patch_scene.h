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

} // namespace cubey::render
