#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/host/common_config.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/windowed_host.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace cubey::host {

struct WindowedAppConfig {
    CommonRunConfig run_config;
    const char* app_name = "cubey";
    const char* ready_status = nullptr;
    VkQueueFlags required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    VkImageUsageFlags swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    std::uint32_t frame_slot_count = 2;
    bool require_dynamic_rendering = true;
    bool require_tessellation_shader = false;
    cubey::vulkan::GpuRuntimeExecutionMode gpu_execution_mode =
        cubey::vulkan::GpuRuntimeExecutionMode::Threaded;
    bool close_on_escape = false;
};

struct WindowedAppCallbacks {
    std::function<void(WindowedAppContext&)> create_global_resources;
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

int run_windowed_app(WindowedAppConfig config, WindowedAppCallbacks callbacks);

} // namespace cubey::host
