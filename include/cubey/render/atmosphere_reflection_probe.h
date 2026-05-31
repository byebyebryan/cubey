#pragma once

#include <cubey/core/math.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cubey::render {

enum class AtmosphereReflectionPrefilterBinding : std::uint32_t {
    FrameUniforms = 0,
    SkyRadianceCube = 1,
};

struct AtmosphereReflectionPrefilterUniforms {
    math::Vec4 right_roughness;
    math::Vec4 up_sample_count;
    math::Vec4 forward_mip_level;
};

static_assert(sizeof(AtmosphereReflectionPrefilterUniforms) == sizeof(float) * 12U);

struct AtmosphereReflectionProbeConfig {
    std::uint32_t extent = 64;
    std::uint32_t mip_levels = 5;
    std::uint32_t irradiance_extent = 0;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    std::uint32_t frame_slot_count = 1;
    AtmosphereBackgroundTextureBindings atmosphere_textures{};
};

struct AtmosphereReflectionProbePipelineConfig {
    std::filesystem::path atmosphere_vertex_shader{};
    std::filesystem::path atmosphere_fragment_shader{};
    std::filesystem::path prefilter_vertex_shader{};
    std::filesystem::path prefilter_fragment_shader{};
    std::filesystem::path irradiance_vertex_shader{};
    std::filesystem::path irradiance_fragment_shader{};
};

struct AtmosphereReflectionProbeUpdateInfo {
    FrameSlot frame_slot{};
    AtmosphereEnvironmentConfig environment{};
};

[[nodiscard]] MaterialPassInfo atmosphere_reflection_prefilter_pass_info();
[[nodiscard]] MaterialPassInfo atmosphere_reflection_irradiance_pass_info();
[[nodiscard]] std::array<ViewRayBasis3D, 6> atmosphere_reflection_probe_cube_face_view_rays();

class AtmosphereReflectionProbe {
  public:
    AtmosphereReflectionProbe() = default;

    AtmosphereReflectionProbe(const AtmosphereReflectionProbe&) = delete;
    AtmosphereReflectionProbe& operator=(const AtmosphereReflectionProbe&) = delete;
    AtmosphereReflectionProbe(AtmosphereReflectionProbe&&) = delete;
    AtmosphereReflectionProbe& operator=(AtmosphereReflectionProbe&&) = delete;

    void create_resources(const cubey::vulkan::Device& device,
                          const AtmosphereReflectionProbeConfig& config);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const AtmosphereReflectionProbePipelineConfig& config);
    void destroy_pipelines();
    void destroy();

    void record_full_update(const cubey::vulkan::CommandRecorder& recorder,
                            const AtmosphereReflectionProbeUpdateInfo& info);
    void record_face_update(const cubey::vulkan::CommandRecorder& recorder,
                            const AtmosphereReflectionProbeUpdateInfo& info,
                            std::uint32_t face_index);

    [[nodiscard]] bool resources_created() const noexcept;
    [[nodiscard]] bool pipelines_created() const noexcept;
    [[nodiscard]] const TextureCube& sky_radiance_cube() const;
    [[nodiscard]] const TextureCube& prefiltered_cube() const;
    [[nodiscard]] const TextureCube& irradiance_cube() const;
    [[nodiscard]] bool irradiance_resources_created() const noexcept;
    [[nodiscard]] std::uint32_t mip_levels() const noexcept {
        return mip_levels_;
    }

  private:
    [[nodiscard]] ColorTargetView face_target(const TextureCube& texture, std::uint32_t extent,
                                              VkFormat format,
                                              const std::vector<cubey::vulkan::ImageView>& views,
                                              std::uint32_t mip_level,
                                              std::uint32_t face_index) const;
    void record_sky_face(const cubey::vulkan::CommandRecorder& recorder, FrameSlot frame_slot,
                         const AtmosphereEnvironmentConfig& environment,
                         std::uint32_t face_index);
    void record_prefilter_face_mip(const cubey::vulkan::CommandRecorder& recorder,
                                   FrameSlot frame_slot, std::uint32_t face_index,
                                   std::uint32_t mip_level);
    void record_irradiance_face(const cubey::vulkan::CommandRecorder& recorder,
                                FrameSlot frame_slot, std::uint32_t face_index);
    void transition_face(const cubey::vulkan::CommandRecorder& recorder, VkImage image,
                         std::uint32_t mip_level, std::uint32_t face_index,
                         VkImageLayout old_layout, VkImageLayout new_layout,
                         VkAccessFlags src_access, VkAccessFlags dst_access,
                         VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) const;
    [[nodiscard]] VkImageLayout current_sky_layout(std::uint32_t face_index) const;
    [[nodiscard]] VkImageLayout current_prefiltered_layout(std::uint32_t mip_level,
                                                          std::uint32_t face_index) const;
    [[nodiscard]] VkImageLayout current_irradiance_layout(std::uint32_t face_index) const;

    std::uint32_t extent_ = 0;
    std::uint32_t mip_levels_ = 0;
    std::uint32_t irradiance_extent_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::optional<TextureCube> sky_radiance_cube_{};
    std::optional<TextureCube> prefiltered_cube_{};
    std::optional<TextureCube> irradiance_cube_{};
    std::vector<cubey::vulkan::ImageView> sky_face_views_{};
    std::vector<cubey::vulkan::ImageView> prefiltered_face_views_{};
    std::vector<cubey::vulkan::ImageView> irradiance_face_views_{};
    std::array<bool, 6> sky_face_initialized_{};
    std::vector<bool> prefiltered_face_mip_initialized_{};
    std::array<bool, 6> irradiance_face_initialized_{};
    AtmosphereBackgroundFrame sky_frame_{};
    std::optional<FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>>
        prefilter_material_{};
    std::optional<FrameUniformMaterialInstance<AtmosphereReflectionPrefilterUniforms>>
        irradiance_material_{};
    std::optional<GraphicsPipelineResource> prefilter_pipeline_{};
    std::optional<GraphicsPipelineResource> irradiance_pipeline_{};
};

} // namespace cubey::render
