#include "spinning_cube_app_internal.h"

#include <utility>

namespace cubey::examples::spinning_cube {

SpinningCubeApp::SpinningCubeApp(SpinningCubeConfig config) : config_(std::move(config)) {}

int SpinningCubeApp::run() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context);
        create_swapchain_resources(context);
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        (void)context;
        (void)timing;
        update_scene_transform();
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        (void)context;
        record_cube_frame(frame);
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_.common,
            .app_name = "spinning_cube",
            .ready_status = "rendering indexed cube",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
        },
        std::move(callbacks));
}

int run_spinning_cube(const SpinningCubeConfig& config) {
    SpinningCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::spinning_cube
