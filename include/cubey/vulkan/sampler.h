#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

struct SamplerConfig {
    VkFilter min_filter = VK_FILTER_LINEAR;
    VkFilter mag_filter = VK_FILTER_LINEAR;
    VkSamplerAddressMode address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode address_mode_u = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    VkSamplerAddressMode address_mode_v = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    VkSamplerAddressMode address_mode_w = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    VkBorderColor border_color = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    VkBool32 compare_enable = VK_FALSE;
    VkCompareOp compare_op = VK_COMPARE_OP_ALWAYS;
    VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
};

[[nodiscard]] VkSamplerCreateInfo sampler_create_info(const SamplerConfig& config);

class Sampler {
  public:
    Sampler(const Device& device, const SamplerConfig& config);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    Sampler(Sampler&& other) noexcept;
    Sampler& operator=(Sampler&& other) noexcept;

    VkSampler handle() const {
        return sampler_;
    }

  private:
    void move_from(Sampler& other) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
