#include <cubey/host/windowed_host.h>

#include <cubey/host/frame_stats.h>
#include <cubey/vulkan/render_context.h>
#include <cubey/vulkan/vk_check.h>

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::host {
namespace {

void validate_config(const WindowedHostConfig& config, const WindowedHostCallbacks& callbacks) {
    if (config.run_config.headless) {
        throw std::runtime_error("windowed host does not support --headless");
    }
    if (config.required_queue_flags == 0) {
        throw std::runtime_error("windowed host requires at least one queue flag");
    }
    if (config.swapchain_image_usage == 0) {
        throw std::runtime_error("windowed host requires swapchain image usage");
    }
    if (config.frame_slot_count == 0) {
        throw std::runtime_error("windowed host requires at least one frame slot");
    }
    if (!callbacks.record_frame) {
        throw std::runtime_error("windowed host requires a record_frame callback");
    }
}

} // namespace

WindowedAppContext::WindowedAppContext(
    const RunConfig& config, GlfwWindow& window, cubey::vulkan::Instance& instance,
    GlfwSurface& surface, cubey::vulkan::Device& device, cubey::vulkan::Swapchain& swapchain,
    cubey::vulkan::FrameResources& frame_resources, cubey::vulkan::GpuRuntime& gpu,
    const cubey::input::InputFrame& input, std::uint32_t frame_slot_count)
    : config_(config), window_(window), instance_(instance), surface_(surface), device_(device),
      swapchain_(swapchain), frame_resources_(frame_resources), gpu_(gpu), input_(input),
      frame_slot_count_(frame_slot_count) {
    cubey::render::validate_frame_slot({.index = 0, .count = frame_slot_count_});
}

WindowedHost::WindowedHost(WindowedHostConfig config, WindowedHostCallbacks callbacks)
    : config_(std::move(config)), callbacks_(std::move(callbacks)) {
    validate_config(config_, callbacks_);
}

WindowedHost::~WindowedHost() {
    if (device_.has_value()) {
        static_cast<void>(vkDeviceWaitIdle(device_->handle()));
    }
    try {
        destroy_swapchain_resources();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "windowed host cleanup failed: %s\n", error.what());
    } catch (...) {
        std::fprintf(stderr, "windowed host cleanup failed\n");
    }
    frame_resources_.reset();
    swapchain_.reset();
    gpu_.reset();
    submission_.reset();
    device_.reset();
    surface_.reset();
    instance_.reset();
    window_.reset();
}

int WindowedHost::run() {
    create_window();
    create_instance();
    create_surface();
    create_device();
    create_submission_coordinator();
    create_gpu_runtime();
    create_swapchain_resources();

    if (callbacks_.on_ready) {
        WindowedAppContext active_context = context();
        callbacks_.on_ready(active_context);
    }
    static_cast<void>(gpu().drain());

    std::uint32_t frame = 0;
    frame_clock_.reset();
    cubey::vulkan::SwapchainRecreateTracker recreate_tracker;
    while (!window().should_close() &&
           (config_.run_config.frames == 0 || frame < config_.run_config.frames)) {
        input_state_.begin_frame();
        window().poll_events();
        if (window().should_close()) {
            break;
        }

        if (window().consume_framebuffer_resized()) {
            std::puts("framebuffer resized; recreating swapchain");
            recreate_swapchain_resources();
            frame_clock_.reset();
            frame_stats_.reset();
            recreate_tracker.reset();
            continue;
        }

        const FrameTiming timing = frame_clock_.tick();
        WindowedAppContext active_context = context();
        if (callbacks_.update) {
            callbacks_.update(active_context, timing);
        }
        static_cast<void>(gpu().drain());
        if (window().should_close()) {
            break;
        }

        cubey::vulkan::RenderFrameResult result = draw_frame(timing);
        if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
            recreate_tracker.record_recreate_request();
            std::puts("swapchain out of date; recreating");
            recreate_swapchain_resources();
            frame_clock_.reset();
            frame_stats_.reset();
            continue;
        }

        recreate_tracker.reset();
        if (callbacks_.frame_stats_sample) {
            std::optional<FrameStatsSample> sample =
                callbacks_.frame_stats_sample(active_context, timing);
            if (sample.has_value()) {
                std::optional<FrameStatsSnapshot> stats = frame_stats_.record_frame(sample.value());
                if (stats.has_value()) {
                    const std::string title =
                        format_window_title(config_.run_config.title, stats.value());
                    window().set_title(title.c_str());
                }
            }
        }
        ++frame;
    }

    cubey::vulkan::check(vkDeviceWaitIdle(device().handle()),
                         "vkDeviceWaitIdle after windowed host");
    destroy_swapchain_resources();
    static_cast<void>(gpu().drain());
    if (callbacks_.shutdown) {
        WindowedAppContext active_context = context();
        callbacks_.shutdown(active_context);
        static_cast<void>(gpu().drain());
    }
    return 0;
}

void WindowedHost::create_window() {
    window_.emplace(GlfwWindowConfig{
        .width = config_.run_config.width,
        .height = config_.run_config.height,
        .title = config_.run_config.title,
    });
    window().set_input_state(&input_state_);
}

void WindowedHost::create_instance() {
    const std::vector<const char*> required_extensions = window().required_instance_extensions();

    cubey::vulkan::InstanceConfig instance_config;
    instance_config.application_name = config_.run_config.title;
    instance_config.required_extensions = required_extensions;
    instance_config.validation = config_.run_config.validation;
    instance_config.require_validation = config_.run_config.require_validation;
    instance_.emplace(instance_config);
}

void WindowedHost::create_surface() {
    surface_.emplace(window(), instance().handle());
}

void WindowedHost::create_device() {
    cubey::vulkan::DeviceConfig device_config;
    device_config.surface = surface().handle();
    device_config.required_queue_flags = config_.required_queue_flags;
    device_config.require_present = true;
    device_config.require_dynamic_rendering = config_.require_dynamic_rendering;
    device_.emplace(instance(), device_config);
}

void WindowedHost::create_submission_coordinator() {
    submission_.emplace(device());
}

void WindowedHost::create_gpu_runtime() {
    gpu_.emplace(cubey::vulkan::GpuRuntimeConfig{
        .device = &device(),
        .submission = &submission(),
        .execution_mode = config_.gpu_execution_mode,
    });
}

void WindowedHost::create_swapchain() {
    window().wait_for_presentable_framebuffer();

    cubey::vulkan::SwapchainConfig swapchain_config;
    swapchain_config.surface = surface().handle();
    swapchain_config.desired_extent = window().framebuffer_extent();
    swapchain_config.image_usage = config_.swapchain_image_usage;
    swapchain_.emplace(device(), swapchain_config);
}

void WindowedHost::create_frame_resources() {
    frame_resources_.emplace(device(), cubey::vulkan::FrameResourcesConfig{
                                           .present_ready_count = swapchain().image_count(),
                                           .frame_slot_count = config_.frame_slot_count,
                                       });
}

void WindowedHost::create_swapchain_resources() {
    create_swapchain();
    create_frame_resources();
    swapchain_resources_created_ = true;
    if (callbacks_.create_swapchain_resources) {
        WindowedAppContext active_context = context();
        callbacks_.create_swapchain_resources(active_context);
    }
    static_cast<void>(gpu().drain());
}

void WindowedHost::destroy_swapchain_resources() {
    if (gpu_.has_value()) {
        static_cast<void>(gpu().drain());
    }
    if (swapchain_resources_created_ && callbacks_.destroy_swapchain_resources) {
        WindowedAppContext active_context = context();
        callbacks_.destroy_swapchain_resources(active_context);
        static_cast<void>(gpu().drain());
    }
    swapchain_resources_created_ = false;
}

void WindowedHost::recreate_swapchain_resources() {
    cubey::vulkan::check(vkDeviceWaitIdle(device().handle()),
                         "vkDeviceWaitIdle before swapchain recreate");
    destroy_swapchain_resources();
    frame_resources_.reset();
    swapchain_.reset();
    create_swapchain_resources();
}

cubey::vulkan::RenderFrameResult WindowedHost::draw_frame(const FrameTiming& timing) {
    cubey::vulkan::RenderContext render_context({
        .device = &device(),
        .swapchain = &swapchain(),
        .frame_resources = &frame_resources(),
        .gpu = &gpu(),
    });

    cubey::vulkan::RenderFrame frame;
    cubey::vulkan::RenderFrameResult result = render_context.begin_frame(&frame);
    if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
        return result;
    }

    WindowedAppContext active_context = context();
    const WindowedRenderFrame render_frame{
        .command_buffer = frame.command_buffer,
        .image_index = frame.image_index,
        .frame_slot =
            {
                .index = frame.frame_slot_index,
                .count = frame.frame_slot_count,
            },
        .color_target = cubey::render::swapchain_color_target_view(swapchain(), frame.image_index),
        .timing = timing,
    };
    callbacks_.record_frame(active_context, render_frame);
    return render_context.end_frame(frame);
}

WindowedAppContext WindowedHost::context() {
    return {config_.run_config,
            window(),
            instance(),
            surface(),
            device(),
            swapchain(),
            frame_resources(),
            gpu(),
            input_state_.frame(),
            frame_resources().frame_slot_count()};
}

GlfwWindow& WindowedHost::window() {
    if (!window_.has_value()) {
        throw std::runtime_error("GLFW window is not initialized");
    }
    return window_.value();
}

cubey::vulkan::Instance& WindowedHost::instance() {
    if (!instance_.has_value()) {
        throw std::runtime_error("Vulkan instance is not initialized");
    }
    return instance_.value();
}

GlfwSurface& WindowedHost::surface() {
    if (!surface_.has_value()) {
        throw std::runtime_error("GLFW surface is not initialized");
    }
    return surface_.value();
}

cubey::vulkan::Device& WindowedHost::device() {
    if (!device_.has_value()) {
        throw std::runtime_error("Vulkan device is not initialized");
    }
    return device_.value();
}

cubey::vulkan::Swapchain& WindowedHost::swapchain() {
    if (!swapchain_.has_value()) {
        throw std::runtime_error("swapchain is not initialized");
    }
    return swapchain_.value();
}

cubey::vulkan::FrameResources& WindowedHost::frame_resources() {
    if (!frame_resources_.has_value()) {
        throw std::runtime_error("frame resources are not initialized");
    }
    return frame_resources_.value();
}

cubey::vulkan::SubmissionCoordinator& WindowedHost::submission() {
    if (!submission_.has_value()) {
        throw std::runtime_error("Vulkan submission coordinator is not initialized");
    }
    return submission_.value();
}

cubey::vulkan::GpuRuntime& WindowedHost::gpu() {
    if (!gpu_.has_value()) {
        throw std::runtime_error("Vulkan GPU runtime is not initialized");
    }
    return gpu_.value();
}

} // namespace cubey::host
