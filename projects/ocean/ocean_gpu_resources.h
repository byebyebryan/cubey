#pragma once

#include "ocean_config.h"

#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace cubey::projects::ocean {

struct OceanGpuResourceConfig {
    OceanConfig ocean{};
    std::filesystem::path shader_dir{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkExtent2D target_extent{};
};

class OceanGpuResources {
  public:
    OceanGpuResources() = default;
    ~OceanGpuResources() = default;

    OceanGpuResources(const OceanGpuResources&) = delete;
    OceanGpuResources& operator=(const OceanGpuResources&) = delete;

    void create(const cubey::vulkan::Device& device, const OceanGpuResourceConfig& config);
    void reset();

    [[nodiscard]] bool initialized() const {
        return sky_pipeline_.has_value() && surface_pipeline_.has_value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& sky_pipeline() const;
    [[nodiscard]] const cubey::render::GraphicsPipelineResource& surface_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& spectrum_init_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& spectrum_evolve_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& fft_pipeline() const;
    [[nodiscard]] const cubey::render::ComputePipelineResource& finalize_pipeline() const;

    [[nodiscard]] VkDescriptorSet spectrum_init_set(std::uint32_t cascade) const;
    [[nodiscard]] VkDescriptorSet spectrum_evolve_set(std::uint32_t cascade) const;
    [[nodiscard]] VkDescriptorSet fft_set(std::uint32_t cascade, std::uint32_t set_index) const;
    [[nodiscard]] VkDescriptorSet finalize_set(std::uint32_t cascade) const;
    [[nodiscard]] VkDescriptorSet surface_set() const;

    [[nodiscard]] std::uint32_t resolution() const {
        return resolution_;
    }

    [[nodiscard]] const cubey::render::Texture2D& h0(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& spectrum(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& ping(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& pong(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& displacement(std::uint32_t cascade) const;
    [[nodiscard]] const cubey::render::Texture2D& normal_foam(std::uint32_t cascade) const;

  private:
    using TextureArray = std::array<std::optional<cubey::render::Texture2D>, kOceanCascadeCount>;

    void create_textures(const cubey::vulkan::Device& device, const OceanConfig& config);
    void create_descriptor_sets(const cubey::vulkan::Device& device);
    void update_descriptors(const cubey::vulkan::Device& device);
    void create_pipelines(const cubey::vulkan::Device& device,
                          const OceanGpuResourceConfig& config);

    [[nodiscard]] const cubey::render::Texture2D&
    texture_at(const TextureArray& textures, std::uint32_t cascade, const char* label) const;
    [[nodiscard]] VkDescriptorSet descriptor_at(std::span<const VkDescriptorSet> sets,
                                                std::uint32_t index, const char* label) const;

    std::uint32_t resolution_ = 0;
    TextureArray h0_{};
    TextureArray spectrum_{};
    TextureArray ping_{};
    TextureArray pong_{};
    TextureArray displacement_{};
    TextureArray normal_foam_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> spectrum_init_layout_;
    std::optional<cubey::vulkan::DescriptorPool> spectrum_init_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount> spectrum_init_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> spectrum_evolve_layout_;
    std::optional<cubey::vulkan::DescriptorPool> spectrum_evolve_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount> spectrum_evolve_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> fft_layout_;
    std::optional<cubey::vulkan::DescriptorPool> fft_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount * 3U> fft_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> finalize_layout_;
    std::optional<cubey::vulkan::DescriptorPool> finalize_pool_;
    std::array<VkDescriptorSet, kOceanCascadeCount> finalize_sets_{};

    std::optional<cubey::vulkan::DescriptorSetLayout> surface_layout_;
    std::optional<cubey::vulkan::DescriptorPool> surface_pool_;
    VkDescriptorSet surface_set_ = VK_NULL_HANDLE;

    std::optional<cubey::render::ComputePipelineResource> spectrum_init_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> spectrum_evolve_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> fft_pipeline_;
    std::optional<cubey::render::ComputePipelineResource> finalize_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> sky_pipeline_;
    std::optional<cubey::render::GraphicsPipelineResource> surface_pipeline_;
};

} // namespace cubey::projects::ocean
