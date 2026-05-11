#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_sampler_config_describes_shadow_sampling() {
    cubey::vulkan::SamplerConfig defaults;
    require(defaults.min_filter == VK_FILTER_LINEAR, "sampler should default to linear min");
    require(defaults.mag_filter == VK_FILTER_LINEAR, "sampler should default to linear mag");
    require(defaults.address_mode == VK_SAMPLER_ADDRESS_MODE_REPEAT,
            "sampler should default to repeat addressing");
    require(defaults.compare_enable == VK_FALSE, "sampler compare should default off");

    const cubey::vulkan::SamplerConfig config{
        .min_filter = VK_FILTER_NEAREST,
        .mag_filter = VK_FILTER_NEAREST,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .compare_enable = VK_TRUE,
        .compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    };

    const VkSamplerCreateInfo info = cubey::vulkan::sampler_create_info(config);
    require(info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            "sampler create info should use sampler structure type");
    require(info.minFilter == VK_FILTER_NEAREST, "sampler create info should preserve min filter");
    require(info.magFilter == VK_FILTER_NEAREST, "sampler create info should preserve mag filter");
    require(info.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            "sampler create info should preserve U address mode");
    require(info.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            "sampler create info should preserve V address mode");
    require(info.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            "sampler create info should preserve W address mode");
    require(info.borderColor == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            "sampler create info should preserve border color");
    require(info.compareEnable == VK_TRUE, "sampler create info should enable compare sampling");
    require(info.compareOp == VK_COMPARE_OP_LESS_OR_EQUAL,
            "sampler create info should preserve compare op");
    require(info.mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST,
            "sampler create info should preserve mipmap mode");
}
