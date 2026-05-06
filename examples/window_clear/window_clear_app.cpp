#include "window_clear_app.h"

#include <cubey/app/windowed_host.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace cubey::examples::window_clear {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

class WindowClearApp {
  public:
    explicit WindowClearApp(RunConfig config) : config_(std::move(config)) {}

    WindowClearApp(const WindowClearApp&) = delete;
    WindowClearApp& operator=(const WindowClearApp&) = delete;

    int run() {
        if (config_.headless) {
            throw std::runtime_error("window_clear does not support --headless yet");
        }

        cubey::app::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            {
                .create_swapchain_resources = {},
                .destroy_swapchain_resources = {},
                .on_ready =
                    [](cubey::app::WindowedAppContext& context) {
                        std::printf("window_clear: %s clearing swapchain at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update = {},
                .record_frame =
                    [](cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                       std::uint32_t image_index, const FrameTiming& timing) {
                        (void)timing;
                        record_clear_frame(context, command_buffer, image_index);
                    },
                .frame_stats_sample = {},
                .shutdown = {},
            });
        return host.run();
    }

  private:
    static void record_clear_frame(cubey::app::WindowedAppContext& context,
                                   VkCommandBuffer command_buffer, std::uint32_t image_index) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        cubey::vulkan::Swapchain& swapchain = context.swapchain();
        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain.images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        VkClearValue clear{};
        clear.color = {{0.02f, 0.025f, 0.035f, 1.0f}};
        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(
                swapchain.image_views().at(swapchain_image_index), clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = swapchain.extent();
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

    RunConfig config_;
};

} // namespace

int run_window_clear(const RunConfig& config) {
    WindowClearApp app(config);
    return app.run();
}

} // namespace cubey::examples::window_clear
