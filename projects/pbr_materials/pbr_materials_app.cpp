#include "pbr_materials_app_internal.h"

#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>

#include <optional>
#include <utility>

namespace cubey::projects::pbr_materials {
namespace detail {

using cubey::host::FrameStatsSample;

PbrMaterialsApp::PbrMaterialsApp(PbrMaterialsConfig config)
    : config_(std::move(config)),
      debug_view_(render::pbr_debug_view_from_name(config_.debug_view)) {
    orbit_controller_.set_home_distance(config_.material == "gallery" ? kCameraDistance : config_.distance);
    orbit_controller_.set_pitch(config_.pitch);
    if (config_.specimen != "sphere" && config_.specimen != "block") throw std::invalid_argument("specimen must be sphere or block");
}

int PbrMaterialsApp::run() {
    if (config_.common.headless) return run_headless();
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
    callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) { draw_ui(); };
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
            .run_config = config_.common,
            .app_name = "pbr_materials",
            .ready_status = "rendering material instance cubes",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int PbrMaterialsApp::run_headless() {
    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& ctx) {
        create_global_resources_if_needed(ctx.device(), ctx.gpu(),
            cubey::host::headless_capture_frame_slot_count(config_.common));
        create_swapchain_resources(ctx.device(), ctx.render_target().extent, ctx.render_target().format);
        update_scene_transform({});
    };
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext& ctx,
        const cubey::host::HeadlessCaptureFrame& f, VkCommandBuffer cmd,
        const cubey::host::HeadlessRenderTarget& target) {
        // Capture motion derives from frame progress, never wall time.
        const auto saved_yaw = config_.yaw;
        config_.yaw += glm::radians(config_.orbit_degrees) * cubey::host::headless_capture_progress(f);
        update_scene_transform(f.timing);
        record_frame(ctx.device(), cmd, target, f.frame_slot, false, ctx.profile_recorder(), f.index);
        config_.yaw = saved_yaw;
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };
    return cubey::host::HeadlessPngHost({.run_config = config_.common,
        .output_format = VK_FORMAT_R8G8B8A8_UNORM}, std::move(callbacks)).run();
}

} // namespace detail

int run_pbr_materials(const PbrMaterialsConfig& config) {
    detail::PbrMaterialsApp app(config);
    return app.run();
}

} // namespace cubey::projects::pbr_materials
