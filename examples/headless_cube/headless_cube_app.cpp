#include "headless_cube_app_internal.h"

#include <utility>

namespace cubey::examples::headless_cube {

HeadlessCubeApp::HeadlessCubeApp(HeadlessCubeConfig config) : config_(std::move(config)) {}

int HeadlessCubeApp::run() {
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_.common;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        create_global_resources_if_needed(context);
    };
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext&,
                                    const cubey::host::HeadlessCaptureFrame& frame,
                                    VkCommandBuffer command_buffer,
                                    const cubey::host::HeadlessRenderTarget& target) {
        render_capture(command_buffer, target, frame);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_resources(); };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

int run_headless_cube(const HeadlessCubeConfig& config) {
    HeadlessCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::headless_cube
