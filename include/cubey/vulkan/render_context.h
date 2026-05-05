#pragma once

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <cstdint>

namespace cubey::vulkan {

enum class RenderFrameResult : std::uint8_t {
    Rendered,
    RecreateSwapchain,
};

struct RenderFrame {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    std::uint32_t image_index = 0;
    bool suboptimal = false;
};

struct RenderContextConfig {
    Device* device = nullptr;
    Swapchain* swapchain = nullptr;
    FrameResources* frame_resources = nullptr;
};

class SwapchainRecreateTracker {
  public:
    explicit SwapchainRecreateTracker(std::uint32_t max_consecutive_recreates = 8);

    void record_recreate_request();
    void reset();

    [[nodiscard]] std::uint32_t consecutive_recreates() const {
        return consecutive_recreates_;
    }

  private:
    std::uint32_t max_consecutive_recreates_ = 0;
    std::uint32_t consecutive_recreates_ = 0;
};

class RenderContext {
  public:
    explicit RenderContext(RenderContextConfig config);

    RenderFrameResult begin_frame(RenderFrame* frame) const;
    RenderFrameResult end_frame(const RenderFrame& frame) const;

  private:
    RenderContextConfig config_;
};

} // namespace cubey::vulkan
