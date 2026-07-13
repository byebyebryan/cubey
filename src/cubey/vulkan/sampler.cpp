#include <cubey/vulkan/sampler.h>

#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::vulkan {
namespace {

VkSamplerAddressMode resolved_address_mode(VkSamplerAddressMode axis_mode,
                                           VkSamplerAddressMode fallback) {
    return axis_mode == VK_SAMPLER_ADDRESS_MODE_MAX_ENUM ? fallback : axis_mode;
}

} // namespace

VkSamplerCreateInfo sampler_create_info(const SamplerConfig& config) {
    auto info = vk_struct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    info.magFilter = config.mag_filter;
    info.minFilter = config.min_filter;
    info.addressModeU = resolved_address_mode(config.address_mode_u, config.address_mode);
    info.addressModeV = resolved_address_mode(config.address_mode_v, config.address_mode);
    info.addressModeW = resolved_address_mode(config.address_mode_w, config.address_mode);
    info.anisotropyEnable = config.max_anisotropy > 1.0F ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = std::max(config.max_anisotropy, 1.0F);
    info.borderColor = config.border_color;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = config.compare_enable;
    info.compareOp = config.compare_op;
    info.mipmapMode = config.mipmap_mode;
    info.minLod = config.min_lod;
    info.maxLod = config.max_lod;
    info.mipLodBias = config.mip_lod_bias;
    return info;
}

Sampler::Sampler(const Device& device, const SamplerConfig& config) : device_(device.handle()) {
    if (device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("sampler creation requires a valid Vulkan device");
    }

    if (!std::isfinite(config.max_anisotropy) || config.max_anisotropy < 1.0F) {
        throw std::runtime_error("sampler max anisotropy must be finite and at least one");
    }
    SamplerConfig resolved = config;
    resolved.max_anisotropy =
        device.sampler_anisotropy_enabled()
            ? std::min(config.max_anisotropy, device.properties().limits.maxSamplerAnisotropy)
            : 1.0F;
    const VkSamplerCreateInfo info = sampler_create_info(resolved);
    check(vkCreateSampler(device_, &info, nullptr, &sampler_), "vkCreateSampler");
}

Sampler::~Sampler() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
    }
}

Sampler::Sampler(Sampler&& other) noexcept {
    move_from(other);
}

Sampler& Sampler::operator=(Sampler&& other) noexcept {
    if (this != &other) {
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
        move_from(other);
    }
    return *this;
}

void Sampler::move_from(Sampler& other) noexcept {
    device_ = other.device_;
    sampler_ = other.sampler_;

    other.device_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

} // namespace cubey::vulkan
