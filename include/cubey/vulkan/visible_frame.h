#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

enum class VisibleFrameResult : std::uint8_t {
    Rendered,
    RecreateSwapchain,
};

struct VisibleFrameContext {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    std::uint32_t image_index = 0;
};

using VisibleFrameRecorder = void (*)(void* user_data, const VisibleFrameContext& context);

struct VisibleFrameConfig {
    Device* device = nullptr;
    Swapchain* swapchain = nullptr;
    FrameResources* frame_resources = nullptr;
    VisibleFrameRecorder recorder = nullptr;
    void* user_data = nullptr;
};

VisibleFrameResult draw_visible_frame(const VisibleFrameConfig& config);

} // namespace cubey::vulkan
