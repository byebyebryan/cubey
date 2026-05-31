#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/core/profiling.h>
#include <cubey/core/run_config.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/glfw_window.h>
#include <cubey/host/ui.h>
#include <cubey/input/input.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/render_context.h>
#include <cubey/vulkan/submission_coordinator.h>
#include <cubey/vulkan/swapchain.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace cubey::host {

class ImGuiOverlay;

class WindowedAppContext {
  public:
    WindowedAppContext(const RunConfig& config, GlfwWindow& window,
                       cubey::vulkan::Instance& instance, GlfwSurface& surface,
                       cubey::vulkan::Device& device, cubey::vulkan::Swapchain& swapchain,
                       cubey::vulkan::FrameResources& frame_resources,
                       cubey::vulkan::GpuRuntime& gpu, const cubey::input::InputFrame& input,
                       std::uint32_t frame_slot_count, UiCaptureState ui_capture,
                       cubey::profiling::ProfileRecorder* profile_recorder);

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
    [[nodiscard]] cubey::vulkan::GpuRuntime& gpu() const {
        return gpu_;
    }
    [[nodiscard]] const cubey::input::InputFrame& input() const {
        return input_;
    }
    [[nodiscard]] cubey::input::FilteredInputFrame filtered_input() const {
        return cubey::host::filtered_input(input_, ui_capture_);
    }
    [[nodiscard]] std::uint32_t frame_slot_count() const {
        return frame_slot_count_;
    }
    [[nodiscard]] bool ui_wants_mouse() const {
        return ui_capture_.wants_mouse;
    }
    [[nodiscard]] bool ui_wants_keyboard() const {
        return ui_capture_.wants_keyboard;
    }
    [[nodiscard]] cubey::profiling::ProfileRecorder* profile_recorder() const {
        return profile_recorder_;
    }

  private:
    const RunConfig& config_;
    GlfwWindow& window_;
    cubey::vulkan::Instance& instance_;
    GlfwSurface& surface_;
    cubey::vulkan::Device& device_;
    cubey::vulkan::Swapchain& swapchain_;
    cubey::vulkan::FrameResources& frame_resources_;
    cubey::vulkan::GpuRuntime& gpu_;
    const cubey::input::InputFrame& input_;
    std::uint32_t frame_slot_count_ = cubey::render::kSingleFrameSlotCount;
    UiCaptureState ui_capture_{};
    cubey::profiling::ProfileRecorder* profile_recorder_ = nullptr;
};

struct WindowedHostConfig {
    RunConfig run_config;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    VkImageUsageFlags swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    std::uint32_t frame_slot_count = 2;
    bool require_dynamic_rendering = true;
    cubey::vulkan::GpuRuntimeExecutionMode gpu_execution_mode =
        cubey::vulkan::GpuRuntimeExecutionMode::Threaded;
};

struct WindowedRenderFrame {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    std::uint32_t image_index = 0;
    cubey::render::FrameSlot frame_slot;
    cubey::render::ColorTargetView color_target;
    FrameTiming timing;
};

struct WindowedHostCallbacks {
    std::function<void(WindowedAppContext&)> create_swapchain_resources;
    std::function<void(WindowedAppContext&)> destroy_swapchain_resources;
    std::function<void(WindowedAppContext&)> on_ready;
    std::function<void(WindowedAppContext&)> draw_ui;
    std::function<void(WindowedAppContext&, const FrameTiming&)> update;
    std::function<void(WindowedAppContext&, const WindowedRenderFrame&)> record_frame;
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
    void create_submission_coordinator();
    void create_gpu_runtime();
    void create_swapchain();
    void create_frame_resources();
    void create_swapchain_resources();
    void destroy_swapchain_resources();
    void shutdown_app_resources();
    void recreate_swapchain_resources();
    void create_profile_recorder();
    void write_profile_outputs();
    [[nodiscard]] cubey::profiling::ProfileRecorder* profile_recorder();
    [[nodiscard]] cubey::profiling::ScopedCpuProfileSpan profile_span(std::uint64_t frame_index,
                                                                      std::string_view label);
    void record_profile_frame(std::uint64_t frame_index, const FrameTiming& timing,
                              const std::optional<FrameStatsSample>& sample);
    cubey::vulkan::RenderFrameResult draw_frame(const FrameTiming& timing);
    [[nodiscard]] std::vector<VkCommandBuffer>
    record_ui_command_buffers(const WindowedRenderFrame& render_frame);
    [[nodiscard]] WindowedAppContext context();

    [[nodiscard]] GlfwWindow& window();
    [[nodiscard]] cubey::vulkan::Instance& instance();
    [[nodiscard]] GlfwSurface& surface();
    [[nodiscard]] cubey::vulkan::Device& device();
    [[nodiscard]] cubey::vulkan::Swapchain& swapchain();
    [[nodiscard]] cubey::vulkan::FrameResources& frame_resources();
    [[nodiscard]] cubey::vulkan::SubmissionCoordinator& submission();
    [[nodiscard]] cubey::vulkan::GpuRuntime& gpu();

    WindowedHostConfig config_;
    WindowedHostCallbacks callbacks_;
    bool swapchain_resources_created_ = false;
    bool shutdown_called_ = false;
    UiCaptureState ui_capture_{};

    std::optional<GlfwWindow> window_;
    std::optional<cubey::vulkan::Instance> instance_;
    std::optional<GlfwSurface> surface_;
    std::optional<cubey::vulkan::Device> device_;
    std::optional<cubey::vulkan::SubmissionCoordinator> submission_;
    std::optional<cubey::vulkan::GpuRuntime> gpu_;
    std::optional<cubey::vulkan::Swapchain> swapchain_;
    std::optional<cubey::vulkan::FrameResources> frame_resources_;
    std::unique_ptr<ImGuiOverlay> imgui_overlay_;
    std::optional<cubey::profiling::ProfileRecorder> profile_recorder_;
    FrameClock frame_clock_;
    FrameStats frame_stats_;
    cubey::input::InputState input_state_;
};

} // namespace cubey::host
