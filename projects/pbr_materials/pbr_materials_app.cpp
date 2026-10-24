#include "pbr_materials_app_internal.h"
#include <algorithm>

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
    orbit_controller_.set_home_distance(
        config_.material == "gallery" && config_.presentation.empty() ? kCameraDistance
                                                                      : config_.distance);
    orbit_controller_.set_pitch(config_.pitch);
    if (config_.specimen != "sphere" && config_.specimen != "block" &&
        config_.specimen != "swatch" && config_.specimen != "cut-block")
        throw std::invalid_argument("specimen must be sphere, block, swatch or cut-block");
    if (!config_.presentation.empty()) {
        (void)presentation_layout(config_.presentation);
        if (config_.material != "gallery")
            throw std::invalid_argument("presentation requires gallery material selection");
    }
    if (!std::isfinite(config_.presentation_time) || config_.presentation_time < -1 ||
        config_.presentation_time > 8)
        throw std::invalid_argument("presentation-time must be -1 or 0..8 seconds");
    presentation_seconds_ = std::max(0.0, config_.presentation_time);
    if (config_.presentation_time >= 0)
        orbit_controller_.toggle_pause();
}

int PbrMaterialsApp::run() {
    if (config_.common.headless)
        return run_headless();
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
        if (!requested_environment_.empty())
            switch_environment(context.device(), context.gpu(), context.swapchain().extent(),
                               context.swapchain().format());
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::D)) {
            debug_view_ = render::next_pbr_debug_view(debug_view_);
        }
        orbit_controller_.update_from_input(input, timing.delta_seconds);
        if (!config_.presentation.empty() && input.key_pressed(cubey::input::Key::R)) {
            presentation_seconds_ = 0;
            orbit_controller_.set_pitch(config_.pitch);
        }
        if (!config_.presentation.empty() && !orbit_controller_.paused())
            presentation_seconds_ = std::min(8.0, presentation_seconds_ + timing.delta_seconds);
        update_scene_transform(timing);
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_workshop_frame(context, frame);
    };
    callbacks.frame_stats_sample =
        [this](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        const auto mesh = config_.specimen == "block"       ? block_mesh_handle_
                          : config_.specimen == "swatch"    ? swatch_mesh_handle_
                          : config_.specimen == "cut-block" ? cut_block_mesh_handle_
                                                            : sphere_mesh_handle_;
        const auto count =
            selected_ >= 0 ? std::size_t{1}
                           : static_cast<std::size_t>(std::count_if(
                                 catalog_.begin(), catalog_.end(), [&](const auto& e) {
                                     return family_filter_ == "all" || family_filter_ == e.family;
                                 }));
        std::uint32_t triangles =
            meshes_.at(mesh).index_count() / 3 * static_cast<std::uint32_t>(count);
        if (!config_.presentation.empty()) {
            triangles = 0;
            for (const auto& item : presentation_layout(config_.presentation)) {
                const auto handle = item.shape == "sphere"   ? sphere_mesh_handle_
                                    : item.shape == "swatch" ? swatch_mesh_handle_
                                                             : cut_block_mesh_handle_;
                triangles += meshes_.at(handle).index_count() / 3;
            }
        }
        return FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = triangles,
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
            .ready_status = "rendering PBR material workshop",
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
        create_global_resources_if_needed(
            ctx.device(), ctx.gpu(),
            cubey::host::headless_capture_frame_slot_count(config_.common));
        create_swapchain_resources(ctx.device(), ctx.render_target().extent,
                                   ctx.render_target().format);
        update_scene_transform({});
    };
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext& ctx,
                                    const cubey::host::HeadlessCaptureFrame& f, VkCommandBuffer cmd,
                                    const cubey::host::HeadlessRenderTarget& target) {
        // Capture motion derives from frame progress, never wall time.
        const auto saved_yaw = config_.yaw;
        if (!config_.presentation.empty())
            presentation_seconds_ = config_.presentation_time >= 0
                                        ? config_.presentation_time
                                        : 8.0 * cubey::host::headless_capture_progress(f);
        config_.yaw +=
            glm::radians(config_.orbit_degrees) * cubey::host::headless_capture_progress(f);
        update_scene_transform(f.timing);
        record_frame(ctx.device(), cmd, target, f.frame_slot, false, ctx.profile_recorder(),
                     f.index);
        config_.yaw = saved_yaw;
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };
    return cubey::host::HeadlessPngHost(
               {.run_config = config_.common, .output_format = VK_FORMAT_R8G8B8A8_UNORM},
               std::move(callbacks))
        .run();
}

} // namespace detail

int run_pbr_materials(const PbrMaterialsConfig& config) {
    detail::PbrMaterialsApp app(config);
    return app.run();
}

} // namespace cubey::projects::pbr_materials
