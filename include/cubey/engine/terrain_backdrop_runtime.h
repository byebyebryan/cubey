#pragma once

#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/pass.h>
#include <cubey/render/target.h>
#include <cubey/render/terrain_backdrop_product.h>
#include <cubey/render/terrain_shadow.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace cubey::vulkan {
class CommandRecorder;
class Device;
class GpuRuntime;
} // namespace cubey::vulkan

namespace cubey::render {
class GraphicsPipelineResource;
}

namespace cubey {

enum class TerrainBackdropDebugView : std::uint8_t {
    Surface = 0,
    Height = 1,
    Slope = 3,
    Clay = 6,
    Normal = 10,
    MaterialWeights = 11,
    AmbientVisibility = 12,
    ProjectedEdge = 14,
    MaterialAlbedo = 15,
    MaterialNormal = 16,
    MaterialRoughness = 18,
    SunVisibility = 19,
    ClassificationNormal = 21,
    Vegetation = 22,
    Moisture = 23,
    AmbientLighting = 24,
    DirectLighting = 25,
    StageOwnership = 27,
};

enum class TerrainBackdropMaterialMode : std::uint8_t {
    Flat,
    FilteredDetail,
};

struct TerrainBackdropRuntimeShaderFiles {
    std::filesystem::path vertex{};
    std::filesystem::path fragment{};
    std::filesystem::path material_compute{};
    std::filesystem::path shadow_vertex{};
};

[[nodiscard]] TerrainBackdropRuntimeShaderFiles
terrain_backdrop_runtime_shader_files(const std::filesystem::path& shader_directory);

struct TerrainBackdropRuntimeCreateInfo {
    const render::TerrainBackdropProduct* product = nullptr;
    TerrainBackdropRuntimeShaderFiles shaders{};
    std::uint64_t material_seed = 0U;
    std::uint32_t frame_slot_count = 1U;
};

struct TerrainBackdropRuntimeTargetInfo {
    VkExtent2D extent{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
};

struct TerrainBackdropRuntimeFrameInfo {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec3 camera_position{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 world_translation{0.0F, 0.0F, 0.0F};
    render::AtmosphereEnvironmentFrameUniforms atmosphere{};
    render::AtmosphereEnvironmentLighting lighting{};
    TerrainBackdropDebugView debug_view = TerrainBackdropDebugView::Surface;
    TerrainBackdropMaterialMode material = TerrainBackdropMaterialMode::FilteredDetail;
    bool shadows_enabled = true;
};

struct TerrainBackdropRuntimeDrawPlan {
    bool center_visible = false;
    std::uint32_t submitted_sector_count = 0U;
    std::uint32_t submitted_triangle_count = 0U;
};

class TerrainBackdropRuntime {
  public:
    TerrainBackdropRuntime();
    ~TerrainBackdropRuntime();

    TerrainBackdropRuntime(const TerrainBackdropRuntime&) = delete;
    TerrainBackdropRuntime& operator=(const TerrainBackdropRuntime&) = delete;
    TerrainBackdropRuntime(TerrainBackdropRuntime&&) noexcept;
    TerrainBackdropRuntime& operator=(TerrainBackdropRuntime&&) noexcept;

    void create(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                const TerrainBackdropRuntimeCreateInfo& info);
    void create_target_resources(const vulkan::Device& device,
                                 const TerrainBackdropRuntimeTargetInfo& target);
    void destroy_target_resources();
    void destroy();

    void replace_product(vulkan::GpuRuntime& gpu, const render::TerrainBackdropProduct& product);
    void prepare_frame(render::FrameSlot frame_slot, const TerrainBackdropRuntimeFrameInfo& info);
    void complete_frame();

    void record_shadow_pass(const vulkan::CommandRecorder& recorder) const;
    void record_surface_draws(const vulkan::CommandRecorder& recorder,
                              render::FrameSlot frame_slot) const;
    void bind_environment(const vulkan::CommandRecorder& recorder,
                          const render::GraphicsPipelineResource& pipeline,
                          render::FrameSlot frame_slot) const;

    [[nodiscard]] bool created() const noexcept;
    [[nodiscard]] bool target_resources_created() const noexcept;
    [[nodiscard]] bool shadow_update_this_frame() const noexcept;
    [[nodiscard]] bool shadow_depth_is_sampled() const noexcept;
    [[nodiscard]] render::DepthTargetView shadow_depth_target() const;
    [[nodiscard]] const render::MaterialPassInfo& shadow_material_pass() const;
    [[nodiscard]] const render::MaterialPassInfo& surface_material_pass() const;
    [[nodiscard]] VkDescriptorSetLayout environment_layout() const;
    [[nodiscard]] const render::TerrainShadowProjection& shadow_projection() const noexcept;
    [[nodiscard]] const render::TerrainShadowCacheState& shadow_cache() const noexcept;
    [[nodiscard]] const TerrainBackdropRuntimeDrawPlan& draw_plan() const noexcept;
    [[nodiscard]] std::uint64_t material_texture_bytes() const noexcept;
    [[nodiscard]] std::uint64_t shadow_caster_triangle_count() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cubey
