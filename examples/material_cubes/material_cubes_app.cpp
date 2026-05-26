#include "material_cubes_app_internal.h"

#include <cubey/host/frame_stats.h>

#include <optional>
#include <utility>

namespace cubey::examples::material_cubes {
namespace detail {

using cubey::host::FrameStatsSample;

MaterialCubesApp::MaterialCubesApp(RunConfig config)
    : config_(std::move(config)),
      debug_view_(render::pbr_debug_view_from_name(config_.debug_view)) {
    orbit_controller_.set_home_distance(kCameraDistance);
}

int MaterialCubesApp::run() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          context.frame_slot_count());
        create_swapchain_resources(context.device(), context.swapchain().extent(),
                                   context.swapchain().format());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::D)) {
            debug_view_ = render::next_pbr_debug_view(debug_view_);
        }
        orbit_controller_.update_from_input(input, timing.delta_seconds);
        update_scene_transform(timing);
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_cube_frame(context, frame);
    };
    callbacks.frame_stats_sample =
        [](cubey::host::WindowedAppContext& context,
           const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        return FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = kCubeTriangleCount * kMaterialCubeCount,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_,
            .app_name = "material_cubes",
            .ready_status = "rendering material instance cubes",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

} // namespace detail

int run_material_cubes(const RunConfig& config) {
    detail::MaterialCubesApp app(config);
    return app.run();
}

} // namespace cubey::examples::material_cubes
