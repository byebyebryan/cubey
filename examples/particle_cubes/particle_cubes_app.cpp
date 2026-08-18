#include "particle_cubes_app_internal.h"

#include <cubey/host/frame_stats.h>

#include <optional>
#include <utility>

namespace cubey::examples::particle_cubes {
namespace {

using cubey::host::FrameStatsSample;
} // namespace

ParticleCubesApp::ParticleCubesApp(RunConfig config) : config_(std::move(config)) {
    orbit_controller_.set_home_distance(kCameraDistance);
}

int ParticleCubesApp::run() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context);
        create_forward_pass(context);
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        orbit_controller_.update_from_input(input, timing.delta_seconds);
        if (input.key_pressed(cubey::input::Key::Space)) {
            paused_ = !paused_;
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_cubes_requested_ = true;
        }
        if (reset_cubes_requested_) {
            reset_particle_buffer(context);
        }
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        (void)context;
        record_particle_cubes_frame(frame);
    };
    callbacks.frame_stats_sample =
        [](cubey::host::WindowedAppContext& context,
           const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        return FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = kParticleCubeCount * kCubeTriangleCount,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = cubey::host::common_run_config_from_legacy(config_),
            .app_name = "particle_cubes",
            .ready_status = "rendering compute-driven cube particles",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int run_particle_cubes(const RunConfig& config) {
    ParticleCubesApp app(config);
    return app.run();
}

} // namespace cubey::examples::particle_cubes
