#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

namespace cubey::vulkan {

struct SamplerConfig {
    VkFilter min_filter = VK_FILTER_LINEAR;
    VkFilter mag_filter = VK_FILTER_LINEAR;
    VkSamplerAddressMode address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
};

class Sampler {
  public:
    Sampler(const Device& device, const SamplerConfig& config);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    VkSampler handle() const {
        return sampler_;
    }

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace cubey::vulkan
