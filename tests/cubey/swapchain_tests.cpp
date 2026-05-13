#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <array>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_swapchain_surface_format_prefers_srgb_color_attachment() {
    const std::vector<VkSurfaceFormatKHR> formats{
        VkSurfaceFormatKHR{
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        },
        VkSurfaceFormatKHR{
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        },
        VkSurfaceFormatKHR{
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        },
    };

    const VkSurfaceFormatKHR selected = cubey::vulkan::choose_swapchain_surface_format(formats);
    require(selected.format == VK_FORMAT_B8G8R8A8_SRGB,
            "swapchain format selection should prefer sRGB attachment formats");
    require(selected.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            "swapchain format selection should preserve the selected color space");
}

void test_swapchain_surface_format_falls_back_to_unorm_when_srgb_is_missing() {
    const std::vector<VkSurfaceFormatKHR> formats{
        VkSurfaceFormatKHR{
            .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        },
        VkSurfaceFormatKHR{
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        },
    };

    const VkSurfaceFormatKHR selected = cubey::vulkan::choose_swapchain_surface_format(formats);
    require(selected.format == VK_FORMAT_R8G8B8A8_UNORM,
            "swapchain format selection should fall back to common UNORM formats");
}

void test_swapchain_surface_format_uses_srgb_default_for_undefined_surface() {
    const std::array<VkSurfaceFormatKHR, 1> formats{
        VkSurfaceFormatKHR{
            .format = VK_FORMAT_UNDEFINED,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        },
    };

    const VkSurfaceFormatKHR selected = cubey::vulkan::choose_swapchain_surface_format(
        std::span<const VkSurfaceFormatKHR>{formats});
    require(selected.format == VK_FORMAT_B8G8R8A8_SRGB,
            "undefined surface format default should be an sRGB attachment");
}
