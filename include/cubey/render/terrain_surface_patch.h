#pragma once

#include <cubey/core/math.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/terrain_surface_patch_material.h>
#include <cubey/vulkan/command_recorder.h>
#include <filesystem>
#include <memory>

namespace cubey::render {

// Shared positive-basis ground/water draw contract. Hydraulic state is supplied
// by the caller as read-only descriptors; this component owns no solver.
struct TerrainSurfacePatchPush {
    math::Mat4 view_projection;
    math::Vec4 grid_cell, camera_wet, presentation, terrain_palette;
};
struct TerrainSurfacePatchUniforms {
    math::Mat4 inverse_view_projection, shadow_view_projection;
    math::Vec4 light_direction_exposure, light_color_mips, clock_encoding;
    math::Vec4 ground_material, surface_material, water_optics, art_direction;
    math::Vec4 terrain_macro, water_film, water_view, terrain_surface, environment_mode;
    math::Vec4 water_shallow_optics, water_agitation, water_rapids, water_stream_foam, water_bank;
};
struct TerrainSurfacePatchFrame {
    math::Mat4 view_projection{1.0F}, shadow_view_projection{1.0F};
    math::Vec3 light_direction{}, light_color{};
    float exposure = 0.4F;
    std::uint32_t prefiltered_mip_levels = 1U;
    VkExtent2D extent{};
    VkFormat target_format = VK_FORMAT_UNDEFINED;
    double visual_clock_s = 0.0, rain_clock_s = 0.0;
    float rain_response = 0.0F;
    std::uint32_t width = 0U, height = 0U, water_view = 0U, surface_mode = 0U;
    bool daylight = false;
    float shadow_base_bias = 0.0F, shadow_slope_bias = 0.0F;
};
[[nodiscard]] TerrainSurfacePatchUniforms
terrain_surface_patch_uniforms(const TerrainSurfacePatchFrame& frame,
                               const TerrainSurfacePatchMaterial& material);
[[nodiscard]] MaterialPassInfo terrain_surface_patch_frame_material();
[[nodiscard]] MaterialPassInfo terrain_surface_patch_mesh_pass(bool water = false);

class TerrainSurfacePatchRenderer {
  public:
    TerrainSurfacePatchRenderer(const vulkan::Device& device, VkExtent2D extent,
                                VkDescriptorSetLayout fields, VkDescriptorSetLayout frame,
                                const std::filesystem::path& shader_directory);
    ~TerrainSurfacePatchRenderer();
    TerrainSurfacePatchRenderer(const TerrainSurfacePatchRenderer&) = delete;
    TerrainSurfacePatchRenderer& operator=(const TerrainSurfacePatchRenderer&) = delete;
    void ground(const vulkan::CommandRecorder& recorder, VkDescriptorSet fields,
                VkDescriptorSet frame, const TerrainSurfacePatchPush& push, std::uint32_t vertices,
                bool bspline, bool legacy = false) const;
    void water(const vulkan::CommandRecorder& recorder, VkDescriptorSet fields,
               VkDescriptorSet frame, const TerrainSurfacePatchPush& push, std::uint32_t vertices,
               bool bspline) const;
    void shadow(const vulkan::CommandRecorder& recorder, VkDescriptorSet fields,
                VkDescriptorSet frame, const TerrainSurfacePatchPush& push, std::uint32_t vertices,
                bool bspline) const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace cubey::render
