#include "window_clear_app.h"

#include <cubey/host/windowed_app.h>
#include <cubey/render/pass.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <utility>

namespace cubey::examples::window_clear {
namespace {

class WindowClearApp {
  public:
    explicit WindowClearApp(RunConfig config) : config_(std::move(config)) {}

    WindowClearApp(const WindowClearApp&) = delete;
    WindowClearApp& operator=(const WindowClearApp&) = delete;

    int run() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.record_frame = [](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
            (void)context;
            record_clear_frame(frame);
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "window_clear",
                .ready_status = "clearing swapchain",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            std::move(callbacks));
    }

  private:
    static void record_clear_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(frame.color_target);
        cubey::render::record_present_render_target_pass(
            recorder, target,
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.02F, 0.025F, 0.035F, 1.0F),
            },
            [](const cubey::vulkan::CommandRecorder&) {});

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
