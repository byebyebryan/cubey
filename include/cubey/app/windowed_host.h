#pragma once

#include <cubey/app/glfw_window.h>
#include <cubey/frame_clock.h>
#include <cubey/frame_stats.h>
#include <cubey/input.h>
#include <cubey/run_config.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/render_context.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace cubey::app {

class WindowedAppContext {
  public:
    WindowedAppContext(const RunConfig& config, GlfwWindow& window,
                       cubey::vulkan::Instance& instance, GlfwSurface& surface,
                       cubey::vulkan::Device& device, cubey::vulkan::Swapchain& swapchain,
                       cubey::vulkan::FrameResources& frame_resources,
                       const cubey::input::InputFrame& input);

    [[nodiscard]] const RunConfig& config() const {
        return config_;
    }
    [[nodiscard]] GlfwWindow& window() const {
        return window_;
    }
    [[nodiscard]] cubey::vulkan::Instance& instance() const {
        return instance_;
    }
    [[nodiscard]] GlfwSurface& surface() const {
        return surface_;
    }
    [[nodiscard]] cubey::vulkan::Device& device() const {
        return device_;
    }
    [[nodiscard]] cubey::vulkan::Swapchain& swapchain() const {
        return swapchain_;
    }
    [[nodiscard]] cubey::vulkan::FrameResources& frame_resources() const {
        return frame_resources_;
    }
    [[nodiscard]] const cubey::input::InputFrame& input() const {
        return input_;
    }

  private:
    const RunConfig& config_;
    GlfwWindow& window_;
    cubey::vulkan::Instance& instance_;
    GlfwSurface& surface_;
    cubey::vulkan::Device& device_;
    cubey::vulkan::Swapchain& swapchain_;
    cubey::vulkan::FrameResources& frame_resources_;
    const cubey::input::InputFrame& input_;
};

struct WindowedHostConfig {
    RunConfig run_config;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    VkImageUsageFlags swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    bool require_dynamic_rendering = true;
};

struct WindowedHostCallbacks {
    std::function<void(WindowedAppContext&)> create_swapchain_resources;
    std::function<void(WindowedAppContext&)> destroy_swapchain_resources;
    std::function<void(WindowedAppContext&)> on_ready;
    std::function<void(WindowedAppContext&, const FrameTiming&)> update;
    std::function<void(WindowedAppContext&, VkCommandBuffer, std::uint32_t, const FrameTiming&)>
        record_frame;
    std::function<std::optional<FrameStatsSample>(WindowedAppContext&, const FrameTiming&)>
        frame_stats_sample;
    std::function<void(WindowedAppContext&)> shutdown;
};

class WindowedHost {
  public:
    WindowedHost(WindowedHostConfig config, WindowedHostCallbacks callbacks);
    ~WindowedHost();

    WindowedHost(const WindowedHost&) = delete;
    WindowedHost& operator=(const WindowedHost&) = delete;

    int run();

  private:
    void create_window();
    void create_instance();
    void create_surface();
    void create_device();
    void create_swapchain();
    void create_frame_resources();
    void create_swapchain_resources();
    void destroy_swapchain_resources();
    void recreate_swapchain_resources();
    cubey::vulkan::RenderFrameResult draw_frame(const FrameTiming& timing);
    [[nodiscard]] WindowedAppContext context();

    [[nodiscard]] GlfwWindow& window();
    [[nodiscard]] cubey::vulkan::Instance& instance();
    [[nodiscard]] GlfwSurface& surface();
    [[nodiscard]] cubey::vulkan::Device& device();
    [[nodiscard]] cubey::vulkan::Swapchain& swapchain();
    [[nodiscard]] cubey::vulkan::FrameResources& frame_resources();

    WindowedHostConfig config_;
    WindowedHostCallbacks callbacks_;
    bool swapchain_resources_created_ = false;

    std::optional<GlfwWindow> window_;
    std::optional<cubey::vulkan::Instance> instance_;
    std::optional<GlfwSurface> surface_;
    std::optional<cubey::vulkan::Device> device_;
    std::optional<cubey::vulkan::Swapchain> swapchain_;
    std::optional<cubey::vulkan::FrameResources> frame_resources_;
    FrameClock frame_clock_;
    FrameStats frame_stats_;
    cubey::input::InputState input_state_;
};

} // namespace cubey::app
