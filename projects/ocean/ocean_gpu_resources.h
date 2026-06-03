#pragma once

#include "ocean_config.h"

#include <cubey/core/math.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cubey::projects::ocean {

struct OceanGpuResourceConfig {
    OceanConfig ocean{};
    std::filesystem::path shader_dir{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkExtent2D target_extent{};
    std::uint32_t frame_slot_count = 1U;
};

struct OceanSurfaceFeatureUniforms {
    cubey::math::Vec4 feature_options;
    cubey::math::Vec4 feature_options2;
    cubey::math::Vec4 material_options;
    cubey::math::Vec4 fade_options;
    cubey::math::Vec4 cascade_options;
};

static_assert(sizeof(OceanSurfaceFeatureUniforms) == sizeof(float) * 20U);

class OceanGpuResources {
  public:
    OceanGpuResources() = default;
    ~OceanGpuResources() = default;

    OceanGpuResources(const OceanGpuResources&) = delete;
    OceanGpuResources& operator=(const OceanGpuResources&) = delete;

    void create(const cubey::vulkan::Device& device, const OceanGpuResourceConfig& config);
    void reset();
    void update_atmosphere_probe_descriptors(const cubey::vulkan::Device& device,
                                             const cubey::render::TextureCube& reflection_probe,
                                             const cubey::render::TextureCube& sky_radiance);
    void update_terrain_ocean_field_descriptor(const cubey::vulkan::Device& device,
                                               const cubey::render::Texture2D& fields);
    void update_terrain_ocean_field_uniform_descriptor(const cubey::vulkan::Device& device,
                                                       cubey::render::FrameSlot frame_slot,
                                                       VkBuffer buffer, VkDeviceSize range);
    void upload_surface_feature_uniforms(cubey::render::FrameSlot frame_slot,
                                         const OceanSurfaceFeatureUniforms& uniforms) const;

    [[nodiscard]] bool initialized() const {
        return surface_pipeline_.has_value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& surface_pipeline() const;
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

  private:
    using TextureArray = std::array<std::optional<cubey::render::Texture2D>, kOceanCascadeCount>;
    using FieldTextureArray = std::array<std::optional<cubey::render::Texture2D>,
                                         kOceanCascadeCount * kOceanSpectrumFieldCount>;

    void create_textures(const cubey::vulkan::Device& device, const OceanConfig& config);
    void create_descriptor_sets(const cubey::vulkan::Device& device,
                                std::uint32_t frame_slot_count);
    void update_descriptors(const cubey::vulkan::Device& device);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const OceanGpuResourceConfig& config);

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

    std::optional<cubey::vulkan::DescriptorSetLayout> spectrum_layout_;
    std::optional<cubey::vulkan::DescriptorPool> spectrum_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount> spectrum_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> modulate_layout_;
    std::optional<cubey::vulkan::DescriptorPool> modulate_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount> modulate_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> fft_layout_;
    std::optional<cubey::vulkan::DescriptorPool> fft_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount * kOceanSpectrumFieldCount * 3U> fft_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> unpack_layout_;
    std::optional<cubey::vulkan::DescriptorPool> unpack_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount> unpack_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> surface_layout_;
    std::optional<cubey::vulkan::DescriptorPool> surface_pool_;
    std::vector<VkDescriptorSet> surface_sets_{};
    std::optional<cubey::render::FrameUniformBuffer<OceanSurfaceFeatureUniforms>>
        surface_feature_uniforms_;

    std::optional<cubey::render::ComputePipelineResource> spectrum_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> modulate_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> fft_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> unpack_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> surface_pipeline_;
};

} // namespace cubey::projects::ocean
