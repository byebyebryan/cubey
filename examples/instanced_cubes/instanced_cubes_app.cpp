#include "instanced_cubes_app_internal.h"

#include <cubey/host/frame_stats.h>

#include <optional>
#include <utility>

namespace cubey::examples::instanced_cubes {
namespace {

using cubey::host::FrameStatsSample;

} // namespace

InstancedCubesApp::InstancedCubesApp(InstancedCubesConfig config) : config_(std::move(config)) {
    orbit_controller_.set_home_distance(kCameraDistance);
}

int InstancedCubesApp::run() {
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
        orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
        update_camera_transform();
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
            .triangles = kCubeTriangleCount * kInstanceCount,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_.common,
            .app_name = "instanced_cubes",
            .ready_status = "rendering instanced cube grid",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int run_instanced_cubes(const InstancedCubesConfig& config) {
    InstancedCubesApp app(config);
    return app.run();
}

} // namespace cubey::examples::instanced_cubes
