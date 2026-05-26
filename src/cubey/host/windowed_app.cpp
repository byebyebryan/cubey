#include <cubey/host/windowed_app.h>

#include <cubey/input/input.h>

#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

namespace cubey::host {
namespace {

void print_ready_status(const WindowedAppConfig& config, WindowedAppContext& context) {
    if (config.ready_status == nullptr) {
        return;
    }
    std::printf("%s: %s %s at %ux%u\n", config.app_name, context.device().device_name(),
                config.ready_status, context.swapchain().extent().width,
                context.swapchain().extent().height);
}

[[nodiscard]] std::function<void(WindowedAppContext&)>
make_draw_ui_callback(WindowedAppCallbacks& callbacks) {
    if (!callbacks.draw_ui) {
        return {};
    }
    return [&callbacks](WindowedAppContext& context) { callbacks.draw_ui(context); };
}

} // namespace

int run_windowed_app(WindowedAppConfig config, WindowedAppCallbacks callbacks) {
    if (config.run_config.headless) {
        throw std::runtime_error(std::string(config.app_name) + " does not support --headless yet");
    }
    if (!callbacks.record_frame) {
        throw std::runtime_error(std::string(config.app_name) +
                                 " requires a record_frame callback");
    }

    bool global_resources_created = false;
    WindowedHost host(
        {
            .run_config = config.run_config,
            .required_queue_flags = config.required_queue_flags,
            .swapchain_image_usage = config.swapchain_image_usage,
            .frame_slot_count = config.frame_slot_count,
            .require_dynamic_rendering = config.require_dynamic_rendering,
            .gpu_execution_mode = config.gpu_execution_mode,
        },
        {
            .create_swapchain_resources =
                [&callbacks, &global_resources_created](WindowedAppContext& context) {
                    if (!global_resources_created && callbacks.create_global_resources) {
                        callbacks.create_global_resources(context);
                        global_resources_created = true;
                    }
                    if (callbacks.create_swapchain_resources) {
                        callbacks.create_swapchain_resources(context);
                    }
                },
            .destroy_swapchain_resources =
                [&callbacks](WindowedAppContext& context) {
                    if (callbacks.destroy_swapchain_resources) {
                        callbacks.destroy_swapchain_resources(context);
                    }
                },
            .on_ready =
                [config, &callbacks](WindowedAppContext& context) {
                    if (callbacks.on_ready) {
                        callbacks.on_ready(context);
                    }
                    print_ready_status(config, context);
                },
            .draw_ui = make_draw_ui_callback(callbacks),
            .update =
                [config, &callbacks](WindowedAppContext& context, const FrameTiming& timing) {
                    const auto input = context.filtered_input();
                    if (config.close_on_escape && input.key_pressed(cubey::input::Key::Escape)) {
                        context.window().request_close();
                    }
                    if (callbacks.update) {
                        callbacks.update(context, timing);
                    }
                },
            .record_frame =
                [&callbacks](WindowedAppContext& context, const WindowedRenderFrame& frame) {
                    callbacks.record_frame(context, frame);
                },
            .frame_stats_sample =
                [&callbacks](WindowedAppContext& context,
                             const FrameTiming& timing) -> std::optional<FrameStatsSample> {
                if (!callbacks.frame_stats_sample) {
                    return std::nullopt;
                }
                return callbacks.frame_stats_sample(context, timing);
            },
            .shutdown =
                [&callbacks](WindowedAppContext& context) {
                    if (callbacks.shutdown) {
                        callbacks.shutdown(context);
                    }
                },
        });
    return host.run();
}

} // namespace cubey::host
