#include "terrain_app_internal.h"

namespace cubey::projects::terrain::detail {

int TerrainApp::run() {
    return config_.common.headless ? run_headless() : run_windowed();
}

void TerrainApp::configure_capture_camera() {
    const auto& c = capture_runtime_->capture();
    const float width = float(c.grid.width - 1U) * c.grid.cell_size_m;
    const float height = float(c.grid.height - 1U) * c.grid.cell_size_m;
    const float domain = std::max(width, height);
    const auto [lo, hi] = std::minmax_element(c.bed_m.begin(), c.bed_m.end());
    capture_target_ = {0.0F, 0.5F * (*lo + *hi), 0.0F};
    float span = domain;
    capture_yaw_ = -0.52F;
    capture_pitch_ = -0.92F;
    const bool landscape = config_.water_capture_camera == "landscape";
    const bool boundary = config_.water_capture_camera == "boundary";
    const bool close = config_.water_capture_camera != "overview" && !landscape;
    if (close) {
        const bool stream = config_.water_capture_camera == "stream";
        float fx = stream ? 0.741F : 0.634F;
        float fz = stream ? 0.562F : 0.521F;
        if (boundary) {
            const auto begin =
                c.depth_m.begin() + std::ptrdiff_t(c.grid.height - 1U) * c.grid.width;
            fx = float(std::max_element(begin, c.depth_m.end()) - begin) /
                 float(c.grid.width - 1U);
            fz = 1.0F;
            capture_yaw_ = 2.6F;
            capture_pitch_ = -0.65F;
        }
        const auto x = std::uint32_t(std::round(fx * float(c.grid.width - 1U)));
        const auto z = std::uint32_t(std::round(fz * float(c.grid.height - 1U)));
        capture_target_ = {(fx - 0.5F) * width, c.bed_m[std::size_t(z) * c.grid.width + x],
                           (fz - 0.5F) * height};
        span = boundary ? 1200.0F : stream ? 900.0F : 1800.0F;
    }
    if (landscape) {
        span = domain * 0.70F;
        capture_yaw_ = 0.25F;
        capture_pitch_ = -0.40F;
    }
    if (config_.captured_water_focus_x_m) {
        const cubey::math::Vec2 world{*config_.captured_water_focus_x_m,
                                      *config_.captured_water_focus_z_m};
        const auto grid = (world - c.grid.sample_origin_xz_m) / c.grid.cell_size_m;
        if (grid.x < 0 || grid.y < 0 || grid.x > float(c.grid.width - 1U) ||
            grid.y > float(c.grid.height - 1U))
            throw std::runtime_error("capture camera focus is outside the captured domain");
        const auto x = std::uint32_t(std::round(grid.x));
        const auto z = std::uint32_t(std::round(grid.y));
        const auto local = world - c.grid.center_xz_m();
        capture_target_ = {local.x, c.bed_m[std::size_t(z) * c.grid.width + x], local.y};
    }
    capture_target_.y = config_.captured_water_focus_height_m.value_or(capture_target_.y);
    span = config_.captured_water_span_m.value_or(span);
    capture_yaw_ = config_.captured_water_yaw_rad.value_or(capture_yaw_);
    capture_pitch_ = config_.captured_water_pitch_rad.value_or(capture_pitch_);
    const float maximum = domain * 4.0F;
    capture_orbit_.set_distance_limits(std::max(16.0F, span * 0.30F), maximum);
    capture_orbit_.set_pitch_limits(-0.38F, 0.38F);
    capture_orbit_.set_home_distance(span * (landscape ? 1.0F : close ? 1.18F : 1.5F));
    capture_orbit_.reset();
    auto far_corner = cubey::math::Vec3{
        std::max(std::abs(-width * 0.5F - capture_target_.x),
                 std::abs(width * 0.5F - capture_target_.x)),
        std::max(std::abs(*lo - capture_target_.y), std::abs(*hi + 100.0F - capture_target_.y)),
        std::max(std::abs(-height * 0.5F - capture_target_.z),
                 std::abs(height * 0.5F - capture_target_.z))};
    if (config_.captured_water_context && capture_runtime_->context_product()) {
        const auto& p = *capture_runtime_->context_product();
        far_corner = glm::max(far_corner, glm::max(glm::abs(p.minimum - capture_target_),
                                                   glm::abs(p.maximum - capture_target_)));
    }
    const float reach =
        maximum + std::hypot(std::hypot(far_corner.x, far_corner.y), far_corner.z);
    capture_camera_.set_projection(std::numbers::pi_v<float> / 3.0F,
                                   std::max(8.0F, span * 0.10F),
                                   reach + std::max(32.0F, reach * 0.05F));
    std::printf("terrain_water_capture_camera: %s target=(%.6f,%.6f,%.6f) span=%.6f\n",
                config_.water_capture_camera.c_str(), capture_target_.x, capture_target_.y,
                capture_target_.z, span);
}

int TerrainApp::run_windowed() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          context.frame_slot_count());
    };
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_swapchain_resources(context.device(), context.swapchain().extent(),
                                   context.swapchain().format(), context.frame_slot_count());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context,
                              const FrameTiming& timing) {
        const cubey::vulkan::GpuSubmissionTicket retire_after =
            context.frame_resources().latest_submitted_ticket();
        poll_product_build(context.device(), context.gpu(), retire_after);
        poll_landscape_build(context.device(), context.gpu(), retire_after);
        poll_atmosphere_atlases(context.device(), context.gpu(), context.frame_resources(),
                                retire_after);
        prepare_capture_context(context.device(), context.gpu());
        if (capture_active_) {
            capture_runtime_->advance_environment(timing.delta_seconds);
            capture_orbit_.update_from_input(context.filtered_input(), timing.delta_seconds);
            if (config_.captured_water_animate)
                capture_animation_s_ += std::clamp(timing.delta_seconds, 0.0, 0.25);
        } else {
            orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
        }
        (void)cubey::atmosphere_environment_advance_time(atmosphere_state_,
                                                         timing.delta_seconds);
        cloud_runtime_.advance(timing.delta_seconds);
    };
    callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        collect_gpu_timings(context.profile_recorder(), frame.timing.frame_index,
                            frame.frame_slot);
        record_target(context.device(), frame.command_buffer, frame.color_target,
                      frame.frame_slot, cubey::render::render_graph_undefined_texture_state(),
                      cubey::render::render_graph_present_texture_state(),
                      cubey::render::RenderGraphCommandBufferMode::BeginAndEnd);
    };
    callbacks.frame_stats_sample =
        [this](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
        return cubey::host::FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = context.swapchain().extent().width,
            .height = context.swapchain().extent().height,
            .triangles = terrain_runtime_.draw_plan().submitted_triangle_count,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        destroy_all_resources(context.gpu());
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_.common,
            .app_name = "terrain",
            .ready_status = "rendering raster terrain backdrop",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int TerrainApp::run_headless() {
    if (config_.captured_water_transition_smoke &&
        (!capture_runtime_ || !config_.common.headless_render_only))
        throw std::runtime_error(
            "capture transition smoke requires a valid capture and headless render-only mode");
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_.common;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    host_config.require_dynamic_rendering = true;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        const std::uint32_t frame_slot_count =
            cubey::host::headless_capture_frame_slot_count(config_.common);
        create_global_resources_if_needed(context.device(), context.gpu(), frame_slot_count);
        finish_atmosphere_atlases(context.device(), context.gpu());
        product_builds_.finish(context.gpu());
        install_ready_product_build(context.device(), context.gpu(), {});
        create_swapchain_resources(context.device(), context.render_target().extent,
                                   context.render_target().format, frame_slot_count);
    };
    if (config_.common.capture_mode == CaptureMode::Video) {
        const float duration_seconds = config_.common.frames > 1U && config_.common.fps > 0U
                                           ? static_cast<float>(config_.common.frames) /
                                                 static_cast<float>(config_.common.fps)
                                           : 0.0F;
        orbit_controller_.set_auto_rotation_speed(
            duration_seconds > 0.0F ? 2.0F * std::numbers::pi_v<float> / duration_seconds
                                    : kTerrainHeadlessOrbitSpeed);
        callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                        const cubey::host::HeadlessCaptureFrame& frame) {
            orbit_controller_.update(frame.timing.delta_seconds);
            (void)cubey::atmosphere_environment_advance_time(atmosphere_state_,
                                                             frame.timing.delta_seconds);
            cloud_runtime_.advance(frame.timing.delta_seconds);
        };
    }
    const auto backdrop_before_frame = callbacks.before_frame;
    const bool context_transition_study = config_.captured_water_context;
    callbacks.before_frame = [this, backdrop_before_frame, context_transition_study](
                                 cubey::host::HeadlessPngContext& context,
                                 const cubey::host::HeadlessCaptureFrame& frame) {
        if (!config_.landscape_smoke_sequence.empty()) {
            if (landscape_smoke_ids_.empty()) {
                std::istringstream input(config_.landscape_smoke_sequence);
                for (std::string id; std::getline(input, id, ',');) {
                    if (id.empty() || landscape_smoke_ids_.size() >= 32)
                        throw std::runtime_error("invalid landscape smoke sequence");
                    landscape_smoke_ids_.push_back(id);
                }
            }
            if (frame.index < landscape_smoke_ids_.size()) {
                const auto before = config_.active_landscape_id;
                const auto& id = landscape_smoke_ids_[frame.index];
                context.gpu().wait_queue_idle("landscape lifecycle frame boundary");
                if (capture_runtime_ && config_.captured_water_audit)
                    capture_runtime_->verify_immutable_fields(context.gpu());
                const bool expected_failure = id.starts_with("!");
                const bool leave_pending = id.starts_with("~");
                const bool asynchronous = id.starts_with("+");
                bool rejected = false;
                try {
                    request_landscape(
                        expected_failure || leave_pending || asynchronous ? id.substr(1) : id);
                    if (!leave_pending && !asynchronous) {
                        landscape_builds_.finish(context.gpu());
                        poll_landscape_build(context.device(), context.gpu(), {});
                    }
                } catch (const std::exception& error) {
                    if (!expected_failure || config_.active_landscape_id != before)
                        throw;
                    rejected = true;
                    std::printf("terrain_landscape_rejected: frame=%u requested=%s showing=%s "
                                "reason=%s\n",
                                frame.index, id.c_str(), before.c_str(), error.what());
                }
                if (expected_failure && !rejected)
                    throw std::runtime_error("expected landscape rejection did not occur");
                // Exercise target rebuild while loaded; no new asset/state.
                if (frame.index == 2U && capture_runtime_) {
                    capture_runtime_->destroy_target_resources();
                    capture_runtime_->ensure_resources(
                        context.device(), context.gpu(),
                        std::uint32_t(capture_profile_slots_.size()),
                        {.extent = {800, 450}, .format = context.render_target().format});
                    capture_runtime_->destroy_target_resources();
                    capture_runtime_->ensure_resources(
                        context.device(), context.gpu(),
                        std::uint32_t(capture_profile_slots_.size()),
                        {.extent = context.render_target().extent,
                         .format = context.render_target().format});
                    std::printf("terrain_landscape_target_rebuild: frame=2 alternate=800x450 "
                                "restored=%ux%u\n",
                                context.render_target().extent.width,
                                context.render_target().extent.height);
                }
                std::printf("terrain_landscape_smoke: frame=%u showing=%s\n", frame.index,
                            config_.active_landscape_id.c_str());
            }
            if (landscape_builds_.busy() && !landscape_smoke_ids_.empty() &&
                landscape_smoke_ids_.back().starts_with("+")) {
                cubey::vulkan::GpuSubmissionTicket latest;
                (void)context.gpu().submit_and_wait(
                    {.label = "landscape asynchronous retirement ticket",
                     .work = [&latest](cubey::vulkan::GpuOwnerContext& owner) {
                         latest = owner.submission().last_submitted();
                     }});
                poll_landscape_build(context.device(), context.gpu(), latest);
            }
        }
        if (config_.captured_water_transition_smoke && frame.index < 8U) {
            capture_active_ = frame.index != 3U && frame.index != 5U;
            config_.captured_water_visible = frame.index != 1U;
            config_.captured_water_animate = false;
            if (context_transition_study)
                config_.captured_water_context = frame.index % 2U == 0U;
            if (!capture_active_ && !backdrop_requested_) {
                request_product_build(runtime_config_, source_choice_index_);
                product_builds_.finish(context.gpu());
                install_ready_product_build(context.device(), context.gpu(), {});
            }
            std::printf("terrain_water_transition: frame=%u mode=%s water=%u context=%u "
                        "fixed_clock=1\n",
                        frame.index, capture_active_ ? "capture" : "backdrop",
                        config_.captured_water_visible ? 1U : 0U,
                        config_.captured_water_context ? 1U : 0U);
        }
        prepare_capture_context(context.device(), context.gpu());
        if (capture_active_) {
            if (config_.terrain_review_orbit_rad_per_s != 0.0F) {
                if (config_.common.capture_mode == CaptureMode::Video)
                    capture_review_s_ =
                        double(frame.index) / double(std::max(1U, config_.common.fps));
                else if (config_.common.headless_render_only)
                    capture_review_s_ += std::clamp(frame.timing.delta_seconds, 0.0, 0.25);
            }
            if (frame.index > 0U)
                capture_runtime_->advance_environment(
                    config_.common.headless_render_only
                        ? frame.timing.delta_seconds
                        : (config_.common.capture_mode == CaptureMode::Video
                               ? 1.0 / double(std::max(1U, config_.common.fps))
                               : 0.0));
            if (config_.captured_water_animate) {
                if (config_.common.capture_mode == CaptureMode::Video)
                    capture_animation_s_ =
                        config_.captured_water_animation_s +
                        double(frame.index) / double(std::max(1U, config_.common.fps));
                else if (config_.common.headless_render_only)
                    capture_animation_s_ += std::clamp(frame.timing.delta_seconds, 0.0, 0.25);
            }
        } else if (backdrop_before_frame)
            backdrop_before_frame(context, frame);
    };
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                    const cubey::host::HeadlessCaptureFrame& frame,
                                    VkCommandBuffer command_buffer,
                                    const cubey::host::HeadlessRenderTarget& target) {
        collect_gpu_timings(context.profile_recorder(), frame.timing.frame_index,
                            frame.frame_slot);
        record_target(context.device(), command_buffer, target, frame.frame_slot,
                      cubey::render::render_graph_color_attachment_texture_state(),
                      cubey::render::render_graph_color_attachment_texture_state(),
                      cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext& context) {
        destroy_all_resources(context.gpu());
    };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}
} // namespace cubey::projects::terrain::detail

namespace cubey::projects::terrain {
using detail::TerrainApp;

int run_terrain(const TerrainProjectConfig& config) {
    TerrainApp app(config);
    return app.run();
}

} // namespace cubey::projects::terrain
