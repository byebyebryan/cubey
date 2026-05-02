#include <cubey/vulkan/sampler.h>

#include <cubey/vulkan/vk_check.h>

#include <stdexcept>

namespace cubey::vulkan {

Sampler::Sampler(const Device& device, const SamplerConfig& config) : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("sampler creation requires a valid Vulkan device");
    }

    auto info = vk_struct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    info.magFilter = config.mag_filter;
    info.minFilter = config.min_filter;
    info.addressModeU = config.address_mode;
    info.addressModeV = config.address_mode;
    info.addressModeW = config.address_mode;
    info.anisotropyEnable = VK_FALSE;
    info.maxAnisotropy = 1.0F;
    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.minLod = 0.0F;
    info.maxLod = 0.0F;
    info.mipLodBias = 0.0F;
    check(vkCreateSampler(device_, &info, nullptr, &sampler_), "vkCreateSampler");
}

Sampler::~Sampler() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
    }
}

} // namespace cubey::vulkan
