#include <cubey/vulkan/swapchain.h>

#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::vulkan {
namespace {

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR flags) {
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };

    for (VkCompositeAlphaFlagBitsKHR choice : choices) {
        if ((flags & choice) != 0) {
            return choice;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D desired_extent) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }

    if (desired_extent.width == 0 || desired_extent.height == 0) {
        throw std::runtime_error("swapchain desired extent must be nonzero");
    }

    return {
        std::clamp(desired_extent.width, caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(desired_extent.height, caps.minImageExtent.height, caps.maxImageExtent.height),
    };
}

} // namespace

VkSurfaceFormatKHR choose_swapchain_surface_format(std::span<const VkSurfaceFormatKHR> formats) {
    if (formats.empty()) {
        throw std::runtime_error("surface reported no formats");
    }
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        return {
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
    }

    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
            (format.format == VK_FORMAT_B8G8R8A8_SRGB ||
             format.format == VK_FORMAT_R8G8B8A8_SRGB)) {
            return format;
        }
    }
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
            format.format == VK_FORMAT_R8G8B8A8_UNORM) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR choose_swapchain_present_mode(std::span<const VkPresentModeKHR> modes,
                                               VkPresentModeKHR requested) {
    if (modes.empty()) {
        throw std::runtime_error("surface reported no present modes");
    }
    const auto supports = [modes](VkPresentModeKHR mode) {
        return std::ranges::find(modes, mode) != modes.end();
    };
    if (supports(requested)) {
        return requested;
    }
    if (requested == VK_PRESENT_MODE_MAILBOX_KHR) {
        if (supports(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        if (supports(VK_PRESENT_MODE_FIFO_KHR)) {
            return VK_PRESENT_MODE_FIFO_KHR;
        }
    }
    throw std::runtime_error("surface does not support requested swapchain present mode");
}

Swapchain::Swapchain(const Device& device, const SwapchainConfig& config)
    : physical_device_(device.physical_device()), device_(device.handle()),
      surface_(config.surface), image_usage_(config.image_usage),
      present_mode_(config.present_mode) {
    if (surface_ == VK_NULL_HANDLE) {
        throw std::runtime_error("swapchain creation requires a Vulkan surface");
    }
    if (device_ == VK_NULL_HANDLE || physical_device_ == VK_NULL_HANDLE) {
        throw std::runtime_error("swapchain creation requires a valid Vulkan device");
    }

    try {
        create(config.desired_extent);
    } catch (...) {
        destroy();
        throw;
    }
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::create(VkExtent2D desired_extent) {
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if ((caps.supportedUsageFlags & image_usage_) != image_usage_) {
        throw std::runtime_error("surface does not support requested swapchain image usage");
    }

    std::uint32_t format_count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR count");
    if (format_count == 0) {
        throw std::runtime_error("surface reported no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count,
                                               formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");

    std::uint32_t present_mode_count = 0;
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count,
                                                    nullptr),
          "vkGetPhysicalDeviceSurfacePresentModesKHR count");
    if (present_mode_count == 0) {
        throw std::runtime_error("surface reported no present modes");
    }
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count,
                                                    present_modes.data()),
          "vkGetPhysicalDeviceSurfacePresentModesKHR");
    present_mode_ = choose_swapchain_present_mode(present_modes, present_mode_);

    surface_format_ = choose_swapchain_surface_format(formats);

    extent_ = choose_extent(caps, desired_extent);

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }

    auto info = vk_struct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = surface_format_.format;
    info.imageColorSpace = surface_format_.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = image_usage_;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha);
    info.presentMode = present_mode_;
    info.clipped = VK_TRUE;

    check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");

    std::uint32_t actual_count = 0;
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, nullptr),
          "vkGetSwapchainImagesKHR count");
    images_.resize(actual_count);
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, images_.data()),
          "vkGetSwapchainImagesKHR");

    image_views_.reserve(images_.size());
    for (VkImage image : images_) {
        image_views_.push_back(create_image_view(image, surface_format_.format));
    }
}

void Swapchain::destroy() {
    destroy_image_views();
    destroy_swapchain();
}

void Swapchain::destroy_image_views() {
    for (VkImageView view : image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    image_views_.clear();
}

void Swapchain::destroy_swapchain() {
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    images_.clear();
}

VkImageView Swapchain::create_image_view(VkImage image, VkFormat format) const {
    auto info = vk_struct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    check(vkCreateImageView(device_, &info, nullptr, &view), "vkCreateImageView");
    return view;
}

} // namespace cubey::vulkan
