#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

enum class FrameResult : std::uint8_t {
    Rendered,
    RecreateSwapchain,
};

struct Frame {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    std::uint32_t image_index = 0;
    bool suboptimal = false;
};

struct RenderContextConfig {
    Device* device = nullptr;
    Swapchain* swapchain = nullptr;
    FrameResources* frame_resources = nullptr;
};

class RenderContext {
  public:
    explicit RenderContext(RenderContextConfig config);

    FrameResult begin_frame(Frame* frame) const;
    FrameResult end_frame(const Frame& frame) const;

  private:
    RenderContextConfig config_;
};

} // namespace cubey::vulkan
