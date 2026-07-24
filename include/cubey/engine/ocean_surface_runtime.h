#pragma once

#include <cubey/core/run_config.h>
#include <cubey/engine/backdrop_reflection.h>
#include <cubey/render/ocean_surface_config.h>

#include <cubey/core/math.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/ocean_surface_frame.h>
#include <cubey/render/ocean_surface_quality.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cubey::vulkan {
class CommandRecorder;
}

namespace cubey {

struct OceanSurfaceRuntimeCreateInfo {
    cubey::render::OceanSurfaceConfig ocean{};
    std::filesystem::path shader_dir{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkExtent2D target_extent{};
    std::uint32_t frame_slot_count = 1U;
};

[[nodiscard]] cubey::render::OceanSurfaceConfig
ocean_surface_config_from_run_config(const RunConfig& config);

struct OceanSurfaceRuntimeFrameInfo {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec3 camera_position_m{0.0F, 20.0F, 0.0F};
    VkExtent2D viewport_extent{};
    float vertical_fov_radians = 0.78539816F;
    float planet_radius_m = 6'360'000.0F;
    float water_datum_m = 0.0F;
    double elapsed_seconds = 0.0;
    double delta_seconds = 1.0 / 60.0;
    cubey::render::AtmosphereEnvironmentLighting lighting{};
    float atmosphere_environment_blend = 1.0F;
    float cloud_environment_blend = 1.0F;
    bool cloud_environment_valid = false;
    cubey::render::OceanRenderView debug_view = cubey::render::OceanRenderView::Final;
    std::uint32_t selected_cascade = 0U;
    bool wire_overlay = false;
    float wire_opacity = 0.35F;
    float exposure = 0.0F;
};

struct OceanSurfaceRuntimeDrawStats {
    std::uint32_t generated_patches = 0U;
    std::uint32_t submitted_patches = 0U;
    std::uint32_t culled_patches = 0U;
    std::uint32_t generated_triangles = 0U;
    std::uint32_t submitted_triangles = 0U;
    std::uint32_t reduced_filter_patches = 0U;
    std::uint32_t reduced_filter_triangles = 0U;
    std::uint32_t reduced_shadow_patches = 0U;
    std::uint32_t reduced_shadow_triangles = 0U;
};

struct OceanSurfaceRuntimeDrawPlan {
    cubey::render::OceanSurfaceFrame surface_frame{};
    OceanSurfaceRuntimeDrawStats stats{};
};

[[nodiscard]] BackdropReflection
ocean_surface_reflection(const cubey::render::OceanSurfaceConfig& config,
                         const OceanSurfaceRuntimeFrameInfo& frame,
                         const cubey::render::OceanSurfaceFrame& surface_frame);

struct OceanSurfaceFeatureUniforms {
    cubey::math::Vec4 feature_options;
    cubey::math::Vec4 feature_options2;
    cubey::math::Vec4 fade_options;
    cubey::math::Vec4 cascade_options;
    cubey::math::Vec4 self_shadow_options;
    cubey::math::Vec4 surface_frame_options;
    cubey::math::Vec4 surface_curve_options;
    cubey::math::Vec4 far_field_options;
    cubey::math::Vec4 far_field_options2;
    cubey::math::Vec4 far_detail_options;
    cubey::math::Vec4 cloud_shadow_world_to_uv_x;
    cubey::math::Vec4 cloud_shadow_world_to_uv_y;
    cubey::math::Vec4 cloud_lighting_options;
    cubey::math::Vec4 atmosphere_environment_options;
    cubey::math::Vec4 cloud_environment_options;
    cubey::math::Vec4 cloud_planar_right_aspect;
    cubey::math::Vec4 cloud_planar_up_tan_half_fovy;
    cubey::math::Vec4 cloud_planar_forward_lod;
    cubey::math::Vec4 cloud_planar_options;
    cubey::math::Vec4 sun_light_direction_intensity;
    cubey::math::Vec4 sun_light_color;
    cubey::math::Vec4 moon_light_direction_intensity;
    cubey::math::Vec4 moon_light_color;
};

static_assert(sizeof(OceanSurfaceFeatureUniforms) == sizeof(float) * 92U);

class OceanSurfaceRuntime {
  public:
    OceanSurfaceRuntime() = default;
    ~OceanSurfaceRuntime() = default;

    OceanSurfaceRuntime(const OceanSurfaceRuntime&) = delete;
    OceanSurfaceRuntime& operator=(const OceanSurfaceRuntime&) = delete;

    void create(const cubey::vulkan::Device& device, const OceanSurfaceRuntimeCreateInfo& config);
    void reset();
    void set_config(const cubey::render::OceanSurfaceConfig& config);
    void reset_simulation();
    void prepare_frame(cubey::render::FrameSlot frame_slot,
                       const OceanSurfaceRuntimeFrameInfo& frame);
    void record_update(const cubey::vulkan::CommandRecorder& recorder,
                       cubey::render::FrameSlot frame_slot);
    void record_surface_draws(const cubey::vulkan::CommandRecorder& recorder,
                              cubey::render::FrameSlot frame_slot) const;
    void update_atmosphere_probe_descriptors(const cubey::vulkan::Device& device,
                                             cubey::render::FrameSlot frame_slot,
                                             const cubey::render::TextureCube& previous,
                                             const cubey::render::TextureCube& current,
                                             const cubey::render::TextureCube& sky_radiance);
    void update_terrain_ocean_field_descriptor(const cubey::vulkan::Device& device,
                                               const cubey::render::Texture2D& fields);
    void update_terrain_ocean_field_uniform_descriptor(const cubey::vulkan::Device& device,
                                                       cubey::render::FrameSlot frame_slot,
                                                       VkBuffer buffer, VkDeviceSize range);
    void update_cloud_shadow_descriptor(const cubey::vulkan::Device& device,
                                        cubey::render::FrameSlot frame_slot, VkSampler sampler,
                                        VkImageView image_view, VkImageLayout image_layout);
    void update_cloud_environment_descriptors(const cubey::vulkan::Device& device,
                                              cubey::render::FrameSlot frame_slot,
                                              const cubey::render::TextureCube& previous,
                                              const cubey::render::TextureCube& current);
    void update_cloud_planar_reflection_descriptor(const cubey::vulkan::Device& device,
                                                   cubey::render::FrameSlot frame_slot,
                                                   const cubey::render::Texture2D& texture);
    void upload_surface_feature_uniforms(cubey::render::FrameSlot frame_slot,
                                         const OceanSurfaceFeatureUniforms& uniforms) const;

    [[nodiscard]] bool initialized() const {
        return surface_pipelines_[0].has_value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource&
    surface_pipeline(cubey::render::OceanDetailFilter filter) const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& spectrum_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& modulate_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& fft_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& unpack_pipeline() const;

    [[nodiscard]] VkDescriptorSet spectrum_set(std::uint32_t cascade) const;
    [[nodiscard]] VkDescriptorSet modulate_set(std::uint32_t cascade) const;
    [[nodiscard]] VkDescriptorSet fft_set(std::uint32_t cascade, std::uint32_t field,
                                          std::uint32_t set_index) const;
    [[nodiscard]] VkDescriptorSet unpack_set(std::uint32_t cascade) const;
    [[nodiscard]] VkDescriptorSet surface_set(cubey::render::FrameSlot frame_slot) const;

    [[nodiscard]] std::uint32_t resolution() const {
        return resolution_;
    }

    [[nodiscard]] const cubey::render::Texture2D& h0(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& field(std::uint32_t cascade,
                                                        std::uint32_t field) const;
    [[nodiscard]] const cubey::render::Texture2D& ping(std::uint32_t cascade,
                                                       std::uint32_t field) const;
    [[nodiscard]] const cubey::render::Texture2D& pong(std::uint32_t cascade,
                                                       std::uint32_t field) const;
    [[nodiscard]] const cubey::render::Texture2D& displacement(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& normal(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& foam(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& fallback_field() const;
    [[nodiscard]] bool cascade_allocated(std::uint32_t cascade) const;
    [[nodiscard]] std::uint32_t cascade_resolution(std::uint32_t cascade) const;
    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* profiler() noexcept {
        return profiler_.has_value() ? &profiler_.value() : nullptr;
    }
    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_timings() const;
    [[nodiscard]] const OceanSurfaceRuntimeDrawPlan& draw_plan() const noexcept {
        return draw_plan_;
    }
    [[nodiscard]] BackdropReflection reflection() const;
    [[nodiscard]] const cubey::render::OceanSurfaceConfig& config() const noexcept {
        return ocean_config_;
    }

  private:
    using TextureArray =
        std::array<std::optional<cubey::render::Texture2D>, cubey::render::kOceanCascadeCount>;
    using FieldTextureArray =
        std::array<std::optional<cubey::render::Texture2D>,
                   cubey::render::kOceanCascadeCount * cubey::render::kOceanSpectrumFieldCount>;
    void create_textures(const cubey::vulkan::Device& device,
                         const cubey::render::OceanSurfaceConfig& config);
    void create_descriptor_sets(const cubey::vulkan::Device& device,
                                std::uint32_t frame_slot_count);
    void update_descriptors(const cubey::vulkan::Device& device);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const OceanSurfaceRuntimeCreateInfo& config);

    [[nodiscard]] const cubey::render::Texture2D&
    texture_at(const TextureArray& textures, std::uint32_t cascade, const char* label) const;
    [[nodiscard]] const cubey::render::Texture2D&
    field_texture_at(const FieldTextureArray& textures, std::uint32_t cascade, std::uint32_t field,
                     const char* label) const;
    [[nodiscard]] VkDescriptorSet descriptor_at(std::span<const VkDescriptorSet> sets,
                                                std::uint32_t index, const char* label) const;

    std::uint32_t resolution_ = 0;
    TextureArray h0_{};
    FieldTextureArray fields_{};
    FieldTextureArray ping_{};
    FieldTextureArray pong_{};
    TextureArray displacement_{};
    TextureArray normal_{};
    TextureArray foam_{};
    std::optional<cubey::render::Texture2D> fallback_field_;
    std::array<bool, cubey::render::kOceanCascadeCount> cascade_allocated_{};
    std::array<std::uint32_t, cubey::render::kOceanCascadeCount> cascade_resolutions_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> spectrum_layout_;
    std::optional<cubey::vulkan::DescriptorPool> spectrum_pool_;
    std::array<VkDescriptorSet, cubey::render::kOceanCascadeCount> spectrum_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> modulate_layout_;
    std::optional<cubey::vulkan::DescriptorPool> modulate_pool_;
    std::array<VkDescriptorSet, cubey::render::kOceanCascadeCount> modulate_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> fft_layout_;
    std::optional<cubey::vulkan::DescriptorPool> fft_pool_;
    std::array<VkDescriptorSet,
               cubey::render::kOceanCascadeCount * cubey::render::kOceanSpectrumFieldCount * 3U>
        fft_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> unpack_layout_;
    std::optional<cubey::vulkan::DescriptorPool> unpack_pool_;
    std::array<VkDescriptorSet, cubey::render::kOceanCascadeCount> unpack_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> surface_layout_;
    std::optional<cubey::vulkan::DescriptorPool> surface_pool_;
    std::vector<VkDescriptorSet> surface_sets_{};
    std::optional<cubey::render::FrameUniformBuffer<OceanSurfaceFeatureUniforms>>
        surface_feature_uniforms_;
    std::optional<cubey::render::FrameUniformBuffer<std::array<cubey::math::Vec4, 2>>>
        fallback_terrain_uniforms_;

    std::optional<cubey::render::ComputePipelineResource> spectrum_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> modulate_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> fft_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> unpack_pipeline_;
    std::array<std::optional<cubey::render::GraphicsPipelineResource>, 3> surface_pipelines_;
    std::optional<cubey::vulkan::GpuTimestampProfiler> profiler_;
    cubey::render::OceanSurfaceConfig ocean_config_{};
    OceanSurfaceRuntimeFrameInfo frame_{};
    OceanSurfaceRuntimeDrawPlan draw_plan_{};
    cubey::render::OceanMeshPatchList patches_{};
    cubey::render::FrameSlot prepared_frame_slot_{};
    std::array<bool, cubey::render::kOceanMaxMeshPatches> visible_patches_{};
    std::array<cubey::render::OceanPatchShadingPlan, cubey::render::kOceanMaxMeshPatches>
        shading_plans_{};
    bool frame_prepared_ = false;
    bool textures_initialized_ = false;
    bool spectrum_initialized_ = false;
    bool foam_initialized_ = false;
    std::uint64_t compute_frame_index_ = 0U;
};

} // namespace cubey
