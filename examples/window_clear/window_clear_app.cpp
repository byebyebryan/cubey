#include "window_clear_app.h"

#include <cubey/app/glfw_window.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/render_context.h>
#include <cubey/vulkan/swapchain.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::examples::window_clear {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

class WindowClearApp {
  public:
    explicit WindowClearApp(RunConfig config) : config_(std::move(config)) {}

    WindowClearApp(const WindowClearApp&) = delete;
    WindowClearApp& operator=(const WindowClearApp&) = delete;

    ~WindowClearApp() {
        if (device_ != VK_NULL_HANDLE) {
            static_cast<void>(vkDeviceWaitIdle(device_));
        }

        frame_resources_.reset();
        swapchain_.reset();

        device_owner_.reset();
        device_ = VK_NULL_HANDLE;
        surface_.reset();
        instance_owner_.reset();
        instance_ = VK_NULL_HANDLE;
        window_.reset();
    }

    int run() {
        if (config_.headless) {
            throw std::runtime_error("window_clear does not support --headless yet");
        }

        init_window();
        create_instance();
        create_surface();
        create_device();
        create_swapchain_resources();
        create_frame_resources();
        render_window();
        return 0;
    }

  private:
    void init_window() {
        window_.emplace(cubey::app::GlfwWindowConfig{
            .width = config_.width,
            .height = config_.height,
            .title = config_.title,
        });
    }

    void create_instance() {
        const std::vector<const char*> required_extensions =
            window().required_instance_extensions();

        cubey::vulkan::InstanceConfig instance_config;
        instance_config.application_name = config_.title;
        instance_config.required_extensions = required_extensions;
        instance_config.validation = config_.validation;
        instance_config.require_validation = config_.require_validation;

        instance_owner_.emplace(instance_config);
        instance_ = instance_owner_->handle();
    }

    void create_surface() {
        surface_.emplace(window(), instance_);
    }

    void create_device() {
        cubey::vulkan::DeviceConfig device_config;
        device_config.surface = surface().handle();
        device_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        device_config.require_present = true;
        device_config.require_dynamic_rendering = true;

        if (!instance_owner_.has_value()) {
            throw std::runtime_error("Vulkan instance must exist before creating a device");
        }
        device_owner_.emplace(instance_owner_.value(), device_config);
        device_ = device_owner_->handle();
    }

    void wait_for_presentable_window_size() const {
        window().wait_for_presentable_framebuffer();
    }

    [[nodiscard]] VkExtent2D current_framebuffer_extent() const {
        return window().framebuffer_extent();
    }

    void create_swapchain_resources() {
        create_swapchain();
    }

    void destroy_swapchain_resources() {
        swapchain_.reset();
    }

    void recreate_swapchain_resources() {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle before swapchain recreate");
        frame_resources_.reset();
        destroy_swapchain_resources();
        create_swapchain_resources();
        create_frame_resources();
    }

    void create_swapchain() {
        wait_for_presentable_window_size();

        cubey::vulkan::SwapchainConfig swapchain_config;
        swapchain_config.surface = surface().handle();
        swapchain_config.desired_extent = current_framebuffer_extent();
        swapchain_config.image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_.emplace(vulkan_device(), swapchain_config);
    }

    void create_frame_resources() {
        frame_resources_.emplace(vulkan_device(), swapchain().image_count());
    }

    void record_clear_frame(VkCommandBuffer command_buffer, std::uint32_t image_index) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain().images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        VkClearValue clear{};
        clear.color = {{0.02f, 0.025f, 0.035f, 1.0f}};
        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(
                swapchain().image_views().at(swapchain_image_index), clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = swapchain().extent();
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdEndRendering(command_buffer);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer window_clear");
    }

    cubey::vulkan::RenderFrameResult draw_frame() {
        cubey::vulkan::RenderContext render_context({
            .device = &vulkan_device(),
            .swapchain = &swapchain(),
            .frame_resources = &frame_resources(),
        });

        cubey::vulkan::RenderFrame frame;
        cubey::vulkan::RenderFrameResult result = render_context.begin_frame(&frame);
        if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
            return result;
        }

        record_clear_frame(frame.command_buffer, frame.image_index);
        return render_context.end_frame(frame);
    }

    void render_window() {
        std::printf("window_clear: %s clearing swapchain at %ux%u\n", vulkan_device().device_name(),
                    swapchain().extent().width, swapchain().extent().height);

        std::uint32_t frame = 0;
        cubey::vulkan::SwapchainRecreateTracker recreate_tracker;
        while (!window().should_close() && (config_.frames == 0 || frame < config_.frames)) {
            window().poll_events();

            if (window().consume_framebuffer_resized()) {
                std::puts("framebuffer resized; recreating swapchain");
                recreate_swapchain_resources();
                recreate_tracker.reset();
                continue;
            }

            cubey::vulkan::RenderFrameResult result = draw_frame();
            if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
                recreate_tracker.record_recreate_request();
                std::puts("swapchain out of date; recreating");
                recreate_swapchain_resources();
                continue;
            }

            recreate_tracker.reset();
            ++frame;
        }

        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle after window_clear");
    }

    cubey::vulkan::Swapchain& swapchain() {
        if (!swapchain_.has_value()) {
            throw std::runtime_error("swapchain is not initialized");
        }
        return swapchain_.value();
    }

    cubey::vulkan::Device& vulkan_device() {
        if (!device_owner_.has_value()) {
            throw std::runtime_error("Vulkan device is not initialized");
        }
        return device_owner_.value();
    }

    cubey::vulkan::FrameResources& frame_resources() {
        if (!frame_resources_.has_value()) {
            throw std::runtime_error("frame resources are not initialized");
        }
        return frame_resources_.value();
    }

    cubey::app::GlfwWindow& window() {
        if (!window_.has_value()) {
            throw std::runtime_error("GLFW window is not initialized");
        }
        return window_.value();
    }

    [[nodiscard]] const cubey::app::GlfwWindow& window() const {
        if (!window_.has_value()) {
            throw std::runtime_error("GLFW window is not initialized");
        }
        return window_.value();
    }

    cubey::app::GlfwSurface& surface() {
        if (!surface_.has_value()) {
            throw std::runtime_error("GLFW surface is not initialized");
        }
        return surface_.value();
    }

    RunConfig config_;

    std::optional<cubey::app::GlfwWindow> window_;
    std::optional<cubey::vulkan::Instance> instance_owner_;
    std::optional<cubey::app::GlfwSurface> surface_;
    std::optional<cubey::vulkan::Device> device_owner_;
    std::optional<cubey::vulkan::Swapchain> swapchain_;
    std::optional<cubey::vulkan::FrameResources> frame_resources_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

} // namespace

int run_window_clear(const RunConfig& config) {
    WindowClearApp app(config);
    return app.run();
}

} // namespace cubey::examples::window_clear
