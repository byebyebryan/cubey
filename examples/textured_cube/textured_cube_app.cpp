#include "textured_cube_app_internal.h"

#include <cubey/host/frame_stats.h>

#include <optional>
#include <utility>

namespace cubey::examples::textured_cube {
namespace {

using cubey::host::FrameStatsSample;

} // namespace

TexturedCubeApp::TexturedCubeApp(RunConfig config) : config_(std::move(config)) {
    orbit_controller_.set_home_distance(kCameraDistance);
}

int TexturedCubeApp::run() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context);
        create_swapchain_resources(context);
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.on_ready = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        orbit_controller_.set_auto_rotation_speed(0.9F);
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context,
                              const FrameTiming& timing) {
        orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
        update_scene_transform(timing);
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        (void)context;
        record_cube_frame(frame);
    };
    callbacks.frame_stats_sample =
        [](cubey::host::WindowedAppContext& context,
           const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        return FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = kCubeTriangleCount,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_,
            .app_name = "textured_cube",
            .ready_status = "rendering interactive compute shaded textured cube",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int run_textured_cube(const RunConfig& config) {
    TexturedCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::textured_cube
