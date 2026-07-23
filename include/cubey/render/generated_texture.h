#pragma once

#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cubey::render {

struct ComputeGeneratedTexture2DConfig {
    std::string label = "compute generated texture";
    VkExtent2D extent{1, 1};
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t mip_levels = 1;
    ShaderStageFile shader{};
    std::uint32_t group_size_x = 1;
    std::uint32_t group_size_y = 1;
    std::uint32_t group_size_z = 1;
    std::span<const std::byte> push_constants{};
    bool create_sampler = true;
    cubey::vulkan::SamplerConfig sampler{};
    VkFormatFeatureFlags required_format_features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
};

[[nodiscard]] std::uint32_t compute_dispatch_group_count(std::uint32_t extent,
                                                         std::uint32_t group_size);
void validate_compute_generated_texture_config(const ComputeGeneratedTexture2DConfig& config);
void validate_compute_generated_texture_format(const cubey::vulkan::Device& device,
                                               const ComputeGeneratedTexture2DConfig& config);
[[nodiscard]] Texture2D
create_compute_generated_texture_2d(const cubey::vulkan::Device& device,
                                    cubey::vulkan::GpuRuntime& gpu,
                                    const ComputeGeneratedTexture2DConfig& config);
[[nodiscard]] Texture2D
create_compute_generated_texture_2d(const cubey::vulkan::Device& device,
                                    cubey::vulkan::GpuOwnerContext& context,
                                    const ComputeGeneratedTexture2DConfig& config);

} // namespace cubey::render
