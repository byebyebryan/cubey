#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>
#include <vector>

namespace cubey::vulkan {

struct SwapchainConfig {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkExtent2D desired_extent{1, 1};
    VkImageUsageFlags image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
};

[[nodiscard]] VkSurfaceFormatKHR
choose_swapchain_surface_format(std::span<const VkSurfaceFormatKHR> formats);

class Swapchain {
  public:
    Swapchain(const Device& device, const SwapchainConfig& config);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    VkSwapchainKHR handle() const {
        return swapchain_;
    }
    VkSurfaceKHR surface() const {
        return surface_;
    }
    VkSurfaceFormatKHR surface_format() const {
        return surface_format_;
    }
    VkFormat format() const {
        return surface_format_.format;
    }
    VkExtent2D extent() const {
        return extent_;
    }
    const std::vector<VkImage>& images() const {
        return images_;
    }
    const std::vector<VkImageView>& image_views() const {
        return image_views_;
    }
    std::size_t image_count() const {
        return images_.size();
    }

  private:
    void create(VkExtent2D desired_extent);
    void destroy();
    void destroy_image_views();
    void destroy_swapchain();
    VkImageView create_image_view(VkImage image, VkFormat format) const;

    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkImageUsageFlags image_usage_ = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format_{};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> image_views_;
};

} // namespace cubey::vulkan
