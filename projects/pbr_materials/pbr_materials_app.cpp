#include "pbr_materials_app_internal.h"
#include <algorithm>
#include <fstream>
#include <iostream>

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
    orbit_controller_.set_home_distance(config_.distance);
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
    if (!std::isfinite(config_.taa_fixture_time) || config_.taa_fixture_time < -1 ||
        config_.taa_fixture_time > 8)
        throw std::invalid_argument("taa-fixture-time must be -1 or 0..8 seconds");
    if (config_.taa_moving_fixture &&
        (!config_.presentation.empty() || config_.lighting_cycle != "frozen"))
        throw std::invalid_argument("TAA moving fixture requires frozen workshop lighting");
    if (config_.lighting_cycle != "frozen" &&
        (config_.presentation.empty() || config_.environment == "studio"))
        throw std::invalid_argument(
            "lighting cycle requires a presentation with clear/clouds lighting");
    if (config_.presentation_time >= 0)
        orbit_controller_.toggle_pause();
    if (!config_.hdr_output.empty() && (!config_.common.headless || config_.common.frames != 1))
        throw std::invalid_argument("HDR diagnostic output requires headless, one-frame capture");
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
        if (input.key_pressed(cubey::input::Key::R)) {
            forward_pbr_renderer().reset_temporal();
            fixture_clock_origin_ = timing.elapsed_seconds;
        }
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
    callbacks.frame_submitted = [this](cubey::host::WindowedAppContext&,
                                       const cubey::host::WindowedRenderFrame& frame,
                                       vulkan::GpuSubmissionTicket ticket) {
        frame_submitted(frame.frame_slot, frame.timing.frame_index, ticket);
    };
    callbacks.frame_discarded = [this](cubey::host::WindowedAppContext&,
                                       const cubey::host::WindowedRenderFrame& frame) {
        frame_discarded(frame.frame_slot);
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
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT |
                                    (config_.anti_aliasing == "taa" ? VK_QUEUE_COMPUTE_BIT : 0U),
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int PbrMaterialsApp::run_headless() {
    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.frame_submitted = [this](cubey::host::HeadlessPngContext&,
                                       const cubey::host::HeadlessCaptureFrame& frame,
                                       vulkan::GpuSubmissionTicket ticket) {
        frame_submitted(frame.frame_slot, frame.index, ticket);
    };
    callbacks.frame_discarded = [this](cubey::host::HeadlessPngContext&,
                                       const cubey::host::HeadlessCaptureFrame& frame) {
        frame_discarded(frame.frame_slot);
    };
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& ctx) {
        create_global_resources_if_needed(
            ctx.device(), ctx.gpu(),
            cubey::host::headless_capture_frame_slot_count(config_.common));
        create_swapchain_resources(ctx.device(), ctx.render_target().extent,
                                   ctx.render_target().format);
        if (!config_.hdr_output.empty())
            hdr_readback_.emplace(ctx.device(), vulkan::readback_buffer_config(
                                                    VkDeviceSize{ctx.render_target().extent.width} *
                                                    ctx.render_target().extent.height * 16U));
        update_scene_transform({});
    };
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext& ctx,
                                    const cubey::host::HeadlessCaptureFrame& f, VkCommandBuffer cmd,
                                    const cubey::host::HeadlessRenderTarget& target) {
        // Capture motion derives from frame progress, never wall time.
        if (!config_.presentation.empty())
            presentation_seconds_ = config_.presentation_time >= 0
                                        ? config_.presentation_time
                                        : 8.0 * cubey::host::headless_capture_progress(f);
        capture_yaw_offset_ =
            glm::radians(config_.orbit_degrees) * cubey::host::headless_capture_progress(f);
        update_scene_transform(f.timing);
        record_frame(ctx.device(), cmd, target, f.frame_slot, false, ctx.profile_recorder(),
                     f.index);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
        try {
            if (hdr_readback_) {
                std::vector<std::byte> bytes(static_cast<std::size_t>(hdr_readback_->size()));
                hdr_readback_->download(bytes.data(), hdr_readback_->size());
                if (config_.hdr_output.has_parent_path())
                    std::filesystem::create_directories(config_.hdr_output.parent_path());
                std::ofstream file(config_.hdr_output, std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
                if (!file)
                    throw std::runtime_error("failed to write scene HDR diagnostic output");
                hdr_readback_.reset();
            }
            if (!config_.view_output.empty())
                save_view(config_.view_output);
        } catch (...) {
            // Export failures must retire GPU objects before the host device.
            hdr_readback_.reset();
            destroy_all_resources();
            throw;
        }
        destroy_all_resources();
    };
    return cubey::host::HeadlessPngHost(
               {.run_config = config_.common,
                .required_queue_flags =
                    VK_QUEUE_GRAPHICS_BIT |
                    (config_.anti_aliasing == "taa" ? VK_QUEUE_COMPUTE_BIT : 0U),
                .output_format = VK_FORMAT_R8G8B8A8_UNORM},
               std::move(callbacks))
        .run();
}

} // namespace detail

int run_pbr_materials(const PbrMaterialsConfig& config) {
    detail::PbrMaterialsApp app(config);
    return app.run();
}

} // namespace cubey::projects::pbr_materials
