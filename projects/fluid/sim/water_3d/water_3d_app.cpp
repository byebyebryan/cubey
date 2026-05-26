#include "water_3d_app.h"

#include "water_3d_commands.h"
#include "water_3d_config.h"
#include "water_3d_diagnostics.h"
#include "water_3d_gpu_resources.h"
#include "water_3d_ui.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/core/profiling.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::water_3d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kCameraDistance = 4.0F;
constexpr float kCameraBaseYaw = -0.45F;
constexpr float kCameraBasePitch = -0.34F;
constexpr cubey::math::Vec3 kVolumeCenter{0.5F, 0.5F, 0.5F};

[[nodiscard]] std::filesystem::path bundled_sample_environment_path() {
#ifdef CUBEY_HDR_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_HDR_SAMPLE_ASSETS_DIR) / "lightroom_14b.hdr";
#else
    return {};
#endif
}

class Water3DApp {
  public:
    Water3DApp(RunConfig config, Water3DAppInfo app_info)
        : config_(std::move(config)), app_info_(app_info), runtime_(1),
          water_config_(water_3d_config_from_run_config(config_)),
          render_view_(water_3d_render_view_from_name(config_.debug_view)) {
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_auto_rotation_speed(0.0F);
    }

    Water3DApp(const Water3DApp&) = delete;
    Water3DApp& operator=(const Water3DApp&) = delete;

    int run() {
        if (config_.headless) {
            return run_headless();
        }

        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_render_pipeline(context.device(), context.swapchain().format(),
                                   context.swapchain().extent());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            const ProjectFrame& project_frame = runtime_.frame_for_timing(timing);
            update_interaction(context, project_frame);
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            const ProjectFrame& project_frame = runtime_.frame_for_timing(frame.timing);
            record_frame(context, frame, project_frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            return record_frame_stats(context, timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
            retire_project_gpu_work();
            detach_project_gpu();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = app_info_.app_name,
                .ready_status = app_info_.ready_status,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

  private:
    void update_interaction(cubey::host::WindowedAppContext& context,
                            const ProjectFrame& project_frame) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::Space)) {
            paused_ = !paused_;
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_simulation();
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            render_view_ = next_render_view(render_view_);
        }

        update_camera_input(context, project_frame.delta_seconds);
    }

    void update_camera_input(cubey::host::WindowedAppContext& context, double delta_seconds) {
        const auto input = context.filtered_input();
        if (input.mouse_enabled()) {
            orbit_controller_.zoom_by_scroll(input.scroll_delta().y);
            if (input.has_cursor()) {
                const cubey::input::CursorPosition cursor = input.cursor();
                if (input.mouse_button_pressed(cubey::input::MouseButton::Left)) {
                    orbit_controller_.begin_drag(cursor.x, cursor.y);
                }
                if (input.mouse_button_down(cubey::input::MouseButton::Left)) {
                    orbit_controller_.drag_to(cursor.x, cursor.y);
                }
            }
        }
        if (!input.mouse_enabled() ||
            input.mouse_button_released(cubey::input::MouseButton::Left) ||
            !input.mouse_button_down(cubey::input::MouseButton::Left)) {
            orbit_controller_.end_drag();
        }
        orbit_controller_.update(delta_seconds);
    }

    std::optional<FrameStatsSample> record_frame_stats(cubey::host::WindowedAppContext& context,
                                                       const FrameTiming& timing) {
        const VkExtent2D extent = context.swapchain().extent();
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const std::uint32_t particle_scan_count =
            water_3d_runtime_particle_scan_count(water_config_, runtime_state_);
        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles =
                render_view_ == Water3DRenderView::Particles
                    ? particle_scan_count * 2U
                    : (is_water_3d_surface_view(render_view_) ? particle_scan_count * 4U + 8U : 2U),
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        draw_water_3d_ui({
            .title = app_info_.ui_title,
            .config = water_config_,
            .runtime_state = runtime_state_,
            .resources = resources_,
            .device = context.device(),
            .latest_frame_stats = latest_frame_stats_,
            .render_view = render_view_,
            .paused = paused_,
            .reset_requested = reset_requested_,
            .latest_fps = latest_fps_,
            .latest_frame_ms = latest_frame_ms_,
        });
    }

    void reset_simulation() {
        reset_requested_ = true;
        runtime_state_ = {};
    }

    [[nodiscard]] Water3DRenderCamera render_camera(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::Transform3D transform = cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = kVolumeCenter,
            .distance = orbit_controller_.distance(),
            .yaw = kCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = kCameraBasePitch + orbit_controller_.pitch(),
        });
        const cubey::math::Quat rotation = transform.rotation;
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .position = transform.translation,
            .right = rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F},
            .up = rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F},
            .forward = rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F},
            .fovy_radians = camera_.fovy_radians(),
        };
    }

    void destroy_swapchain_resources() {
        resources_.destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        surface_graph_executor_.clear();
        resources_.destroy_all_resources();
        ibl_environment_.reset();
    }

    [[nodiscard]] std::filesystem::path resolved_environment_path() const {
        if (!config_.pbr.environment_path.empty()) {
            if (!std::filesystem::exists(config_.pbr.environment_path)) {
                throw std::runtime_error("environment HDR does not exist: " +
                                         config_.pbr.environment_path.string());
            }
            return config_.pbr.environment_path;
        }

        const std::filesystem::path sample = bundled_sample_environment_path();
        if (!sample.empty() && std::filesystem::exists(sample)) {
            return sample;
        }
        return {};
    }

    void create_environment_resources_if_needed(cubey::vulkan::Device& device,
                                                cubey::vulkan::GpuRuntime& gpu) {
        if (ibl_environment_.has_value()) {
            return;
        }

        cubey::render::GeneratedPbrEnvironmentConfig ibl_config;
        ibl_config.intensity = 1.0F;
        const std::filesystem::path environment = resolved_environment_path();
        if (!environment.empty()) {
            const cubey::asset::HdrImage image = cubey::asset::load_hdr_image(environment);
            ibl_environment_.emplace(cubey::render::create_pbr_environment_from_equirectangular(
                device, gpu,
                cubey::render::PbrEquirectangularImage{
                    .width = image.width,
                    .height = image.height,
                    .rgba32f = image.rgba32f,
                },
                ibl_config));
            return;
        }

        ibl_environment_.emplace(
            cubey::render::create_generated_pbr_environment(device, gpu, ibl_config));
    }

    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const {
        if (!ibl_environment_.has_value()) {
            throw std::runtime_error("water 3D IBL environment is not initialized");
        }
        return ibl_environment_.value();
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        create_environment_resources_if_needed(device, gpu);
        attach_project_gpu(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), water_config_,
                                                     frame_slot_count);
        surface_graph_executor_.resize(frame_slot_count);
    }

    void attach_project_gpu(cubey::vulkan::GpuRuntime& gpu) {
        runtime_.attach_gpu_if_needed(gpu);
    }

    void detach_project_gpu() {
        runtime_.detach_gpu_if_attached();
    }

    void retire_project_gpu_work() {
        static_cast<void>(runtime_.retire_completed_gpu_work());
    }

    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent) {
        resources_.create_render_pipeline(device, color_format, extent, ibl_environment());
    }

    void record_frame(cubey::host::WindowedAppContext& context,
                      const cubey::host::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
        if (profiler != nullptr) {
            profiler->collect(render_frame.frame_slot.index);
            record_gpu_timings(context.profile_recorder(),
                               collected_profile_frame_index(frame, render_frame.frame_slot),
                               resources_.latest_timings());
            maybe_print_gpu_timings(frame);
        }
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (profiler != nullptr) {
            profiler->begin_frame(render_frame.command_buffer, render_frame.frame_slot.index);
        }
        record_water_3d_compute(render_frame.command_buffer, resources_, water_config_,
                                runtime_state_, render_frame.frame_slot, paused_, reset_requested_,
                                frame, true, profiler);
        if (is_water_3d_surface_view(render_view_)) {
            record_water_3d_surface_draw(
                render_frame.command_buffer, context.device(), surface_graph_executor_, resources_,
                water_config_, render_frame.frame_slot, runtime_state_, render_view_,
                render_camera(render_frame.color_target.extent), render_frame.color_target,
                Water3DRenderTargetMode::Present, ibl_environment(), profiler);
        } else {
            cubey::render::record_present_render_target(
                recorder, cubey::render::render_target_view(render_frame.color_target),
                [this, &render_frame,
                 profiler](const cubey::vulkan::CommandRecorder& present_recorder) {
                    record_water_3d_draw(present_recorder.handle(), resources_, water_config_,
                                         render_frame.frame_slot, runtime_state_, render_view_,
                                         render_camera(render_frame.color_target.extent),
                                         render_frame.color_target, profiler);
                });
        }
        recorder.end("vkEndCommandBuffer water_3d");
    }

    void maybe_print_gpu_timings(const ProjectFrame& frame) {
        if (!config_.print_frame_stats) {
            return;
        }
        const std::vector<cubey::vulkan::GpuPassTiming>& timings = resources_.latest_timings();
        if (timings.empty()) {
            return;
        }
        if (last_gpu_timing_print_seconds_ >= 0.0 &&
            frame.elapsed_seconds - last_gpu_timing_print_seconds_ < 1.0) {
            return;
        }
        last_gpu_timing_print_seconds_ = frame.elapsed_seconds;
        std::printf("water_3d_gpu:");
        for (const cubey::vulkan::GpuPassTiming& timing : timings) {
            std::printf(" %s=%.3fms", timing.label.c_str(), timing.milliseconds);
        }
        std::printf("\n");
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          cubey::render::FrameSlot frame_slot,
                                          const ProjectFrame& frame,
                                          cubey::profiling::ProfileRecorder* profile_recorder) {
        const std::uint64_t frame_index = profile_frame_index(frame);
        static_cast<void>(gpu.submit_and_wait({
            .label = "water_3d headless simulation frame",
            .work =
                [this, frame_slot, frame, profile_recorder,
                 frame_index](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
                    if (profiler != nullptr) {
                        profiler->begin_frame(commands.command_buffer(), frame_slot.index);
                    }
                    record_water_3d_compute(commands.command_buffer(), resources_, water_config_,
                                            runtime_state_, frame_slot, paused_, reset_requested_,
                                            frame, true, profiler);
                    commands.submit_and_wait();
                    if (profiler != nullptr) {
                        profiler->collect(frame_slot.index);
                        record_gpu_timings(profile_recorder, frame_index,
                                           resources_.latest_timings());
                    }
                },
        }));
        if (should_record_water_3d_diagnostics(profile_recorder, water_config_, frame_index)) {
            const std::vector<std::uint8_t> diagnostics = gpu.readback_buffer(
                resources_.diagnostics().handle(), resources_.diagnostics().size(),
                "water_3d diagnostics readback");
            record_water_3d_diagnostics(*profile_recorder, frame_index, water_config_, diagnostics);
        }
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_global_resources_if_needed(
                context.device(), context.gpu(),
                cubey::host::headless_capture_frame_slot_count(config_));
            create_render_pipeline(context.device(), target.format, target.extent);
        };
        cubey::host::install_headless_simulation_driver(
            callbacks, config_,
            {
                .png_frame_count = water_3d_headless_frame_count(config_),
                .png_timing =
                    [this](std::uint64_t simulation_frame) {
                        return fixed_water_3d_headless_timing(water_config_, simulation_frame);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext& context,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
                        record_headless_simulation_frame(runtime_.gpu(), frame.frame_slot,
                                                         project_frame, context.profile_recorder());
                    },
            });
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            if (is_water_3d_surface_view(render_view_)) {
                record_water_3d_surface_draw(
                    command_buffer, context.device(), surface_graph_executor_, resources_,
                    water_config_, frame.frame_slot, runtime_state_, render_view_,
                    render_camera(target.extent), target, Water3DRenderTargetMode::ColorAttachment,
                    ibl_environment());
            } else {
                record_water_3d_draw(command_buffer, resources_, water_config_, frame.frame_slot,
                                     runtime_state_, render_view_, render_camera(target.extent),
                                     target);
            }
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_all_resources();
            retire_project_gpu_work();
            detach_project_gpu();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    RunConfig config_;
    Water3DAppInfo app_info_;
    cubey::ProjectRuntimeAdapter runtime_;
    Water3DConfig water_config_;
    Water3DRuntimeState runtime_state_;
    Water3DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor surface_graph_executor_;
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_{0.25};
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    Water3DRenderView render_view_ = Water3DRenderView::Surface;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    double last_gpu_timing_print_seconds_ = -1.0;
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_water_3d(const RunConfig& config, Water3DAppInfo app_info) {
    Water3DApp app(config, app_info);
    return app.run();
}

} // namespace cubey::projects::fluid::water_3d
