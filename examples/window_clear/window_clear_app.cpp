#include "window_clear_app.h"

#include <cubey/host/windowed_host.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace cubey::examples::window_clear {
namespace {

class WindowClearApp {
  public:
    explicit WindowClearApp(RunConfig config) : config_(std::move(config)) {}

    WindowClearApp(const WindowClearApp&) = delete;
    WindowClearApp& operator=(const WindowClearApp&) = delete;

    int run() {
        if (config_.headless) {
            throw std::runtime_error("window_clear does not support --headless yet");
        }

        cubey::host::WindowedHost host(
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
                    [](cubey::host::WindowedAppContext& context) {
                        std::printf("window_clear: %s clearing swapchain at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update = {},
                .record_frame =
                    [](cubey::host::WindowedAppContext& context,
                       const cubey::host::WindowedRenderFrame& frame) {
                        (void)context;
                        record_clear_frame(frame);
                    },
                .frame_stats_sample = {},
                .shutdown = {},
            });
        return host.run();
    }

  private:
    static void record_clear_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        recorder.transition_image_layout(
            cubey::vulkan::begin_color_attachment_transition(frame.color_target.image));

        VkClearValue clear{};
        clear.color = {{0.02f, 0.025f, 0.035f, 1.0f}};
        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(frame.color_target);
        cubey::render::RenderClearValues clear_values;
        clear_values.color = clear;
        const cubey::render::RenderTargetRenderingInfo rendering(target, clear_values);

        recorder.begin_rendering(rendering.info());
        recorder.end_rendering();

        recorder.transition_image_layout(
            cubey::vulkan::finish_color_attachment_for_present_transition(
                frame.color_target.image));

        recorder.end("vkEndCommandBuffer window_clear");
    }

    RunConfig config_;
};

} // namespace

int run_window_clear(const RunConfig& config) {
    WindowClearApp app(config);
    return app.run();
}

} // namespace cubey::examples::window_clear
