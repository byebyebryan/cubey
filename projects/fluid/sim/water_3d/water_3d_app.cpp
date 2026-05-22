#include "water_3d_app.h"

#include "water_3d_commands.h"
#include "water_3d_config.h"
#include "water_3d_gpu_resources.h"

#include <cubey/asset/hdr_image.h>
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

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::water_3d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kCameraDistance = 2.7F;
constexpr float kCameraBaseYaw = -0.52F;
constexpr float kCameraBasePitch = -0.38F;
constexpr cubey::math::Vec3 kVolumeCenter{0.5F, 0.5F, 0.5F};

constexpr std::array<Water3DRenderView, 10> kRenderViews{
    Water3DRenderView::Surface,
    Water3DRenderView::Particles,
    Water3DRenderView::Cells,
    Water3DRenderView::Velocity,
    Water3DRenderView::Pressure,
    Water3DRenderView::Solid,
    Water3DRenderView::Overpack,
    Water3DRenderView::SurfaceDepth,
    Water3DRenderView::SurfaceThickness,
    Water3DRenderView::SurfaceNormals,
};

constexpr std::array<Water3DTransferMode, 2> kTransferModes{
    Water3DTransferMode::Apic,
    Water3DTransferMode::PicFlip,
};

[[nodiscard]] double bytes_to_mib(VkDeviceSize bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

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
        const cubey::input::InputFrame& input = context.input();
        if (!context.ui_wants_keyboard()) {
            if (input.key_pressed(cubey::input::Key::Space)) {
                paused_ = !paused_;
            }
            if (input.key_pressed(cubey::input::Key::R)) {
                reset_simulation();
            }
            if (input.key_pressed(cubey::input::Key::D)) {
                render_view_ = next_render_view(render_view_);
            }
        }

        update_camera_input(context, project_frame.delta_seconds);
    }

    void update_camera_input(cubey::host::WindowedAppContext& context, double delta_seconds) {
        const cubey::input::InputFrame& input = context.input();
        if (!context.ui_wants_mouse()) {
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
        if (context.ui_wants_mouse() ||
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
        ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(app_info_.ui_title)) {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Paused", &paused_);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            reset_simulation();
        }

        if (ImGui::BeginCombo("Render view", water_3d_render_view_name(render_view_))) {
            for (Water3DRenderView view : kRenderViews) {
                const bool selected = view == render_view_;
                if (ImGui::Selectable(water_3d_render_view_name(view), selected)) {
                    render_view_ = view;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Transfer",
                              water_3d_transfer_mode_name(water_config_.transfer_mode))) {
            for (Water3DTransferMode mode : kTransferModes) {
                const bool selected = mode == water_config_.transfer_mode;
                if (ImGui::Selectable(water_3d_transfer_mode_name(mode), selected)) {
                    water_config_.transfer_mode = mode;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        int pressure_iterations = static_cast<int>(water_config_.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 128)) {
            water_config_.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        int substeps = static_cast<int>(water_config_.substeps);
        if (ImGui::SliderInt("Substeps", &substeps, 1, 4)) {
            water_config_.substeps = static_cast<std::uint32_t>(substeps);
        }
        ImGui::SliderFloat("PIC/FLIP blend", &water_config_.flip_ratio, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Velocity limit", &water_config_.velocity_limit, 1.0F, 8.0F, "%.2f");
        ImGui::SliderFloat("Particle damping", &water_config_.particle_damping, 0.980F, 1.000F,
                           "%.3f");
        ImGui::SliderFloat("Particle volume strength", &water_config_.particle_volume_strength,
                           0.0F, 48.0F, "%.1f");
        ImGui::SliderFloat("Particle radius", &water_config_.particle_radius, 0.004F, 0.040F,
                           "%.4f");
        ImGui::SliderFloat("Surface thickness", &water_config_.surface_thickness_scale, 0.1F, 4.0F,
                           "%.2f");
        ImGui::SliderFloat("Surface smooth px", &water_config_.surface_smoothing_radius_px, 0.0F,
                           12.0F, "%.1f");
        ImGui::SliderFloat("Surface depth sigma", &water_config_.surface_depth_sigma, 0.005F,
                           0.120F, "%.3f");
        ImGui::SliderFloat("Surface absorption", &water_config_.surface_absorption, 0.0F, 5.0F,
                           "%.2f");
        ImGui::SliderFloat("Surface refraction", &water_config_.surface_refraction_strength, 0.0F,
                           0.12F, "%.3f");
        ImGui::SliderFloat("Environment intensity", &water_config_.environment_intensity, 0.0F,
                           4.0F, "%.2f");
        ImGui::SliderFloat("Environment rotation", &water_config_.environment_rotation_degrees,
                           -180.0F, 180.0F, "%.0f deg");
        ImGui::SliderFloat("Exposure", &water_config_.exposure, -4.0F, 4.0F, "%.2f");
        ImGui::SliderFloat("Gravity", &water_config_.gravity, -4.0F, 0.0F, "%.2f");
        ImGui::SliderFloat("Boundary bounce", &water_config_.boundary_restitution, 0.0F, 0.8F,
                           "%.2f");
        if (ImGui::SliderFloat("Fill width", &water_config_.initial_fill_width,
                               kWater3DMinFillFraction, kWater3DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(water_config_);
            reset_simulation();
        }
        if (ImGui::SliderFloat("Fill height", &water_config_.initial_fill_height,
                               kWater3DMinFillFraction, kWater3DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(water_config_);
            reset_simulation();
        }
        if (ImGui::SliderFloat("Fill depth", &water_config_.initial_fill_depth,
                               kWater3DMinFillFraction, kWater3DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(water_config_);
            reset_simulation();
        }
        ImGui::SliderFloat("Slice depth", &water_config_.slice_depth, 0.02F, 0.98F, "%.2f");

        ImGui::Text("Grid: %u x %u x %u", water_config_.grid_width, water_config_.grid_height,
                    water_config_.grid_depth);
        const std::uint32_t scanned_particles =
            water_3d_runtime_particle_scan_count(water_config_, runtime_state_);
        ImGui::Text("Particles: %u reset / %u capacity", water_config_.active_particle_count,
                    water_config_.particle_capacity);
        ImGui::Text("Compute particles: %u scanned", scanned_particles);
        if (latest_frame_stats_.has_value()) {
            ImGui::Text("Frame: %.1f fps / %.2f ms avg (%.2f ms last)", latest_frame_stats_->fps,
                        latest_frame_stats_->frame_ms, latest_frame_ms_);
        } else if (latest_fps_ > 0.0) {
            ImGui::Text("Frame: %.1f fps / %.2f ms", latest_fps_, latest_frame_ms_);
        } else {
            ImGui::TextUnformatted("Frame: collecting...");
        }

        const VkDeviceSize water_bytes = resources_.allocated_buffer_bytes();
        const cubey::vulkan::DeviceMemoryBudgetInfo memory_budget =
            context.device().device_memory_budget();
        ImGui::Text("Water GPU buffers: %.1f MiB", bytes_to_mib(water_bytes));
        if (memory_budget.available && memory_budget.device_local_budget > 0) {
            ImGui::Text("VRAM: %.0f / %.0f MiB used",
                        bytes_to_mib(memory_budget.device_local_usage),
                        bytes_to_mib(memory_budget.device_local_budget));
        } else {
            ImGui::Text("VRAM heap: %.0f MiB (usage unavailable)",
                        bytes_to_mib(memory_budget.device_local_heap_size));
        }
        ImGui::End();
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
        if (!config_.environment_path.empty()) {
            if (!std::filesystem::exists(config_.environment_path)) {
                throw std::runtime_error("environment HDR does not exist: " +
                                         config_.environment_path.string());
            }
            return config_.environment_path;
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
        if (!runtime_.has_gpu()) {
            runtime_.attach_gpu(gpu);
        }
    }

    void detach_project_gpu() {
        if (runtime_.has_gpu()) {
            runtime_.detach_gpu();
        }
    }

    void retire_project_gpu_work() {
        if (runtime_.has_gpu()) {
            static_cast<void>(runtime_.gpu().retire_deferred_destruction());
            return;
        }
        static_cast<void>(runtime_.retire_deferred_destruction());
    }

    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent) {
        resources_.create_render_pipeline(device, color_format, extent, ibl_environment());
    }

    void record_frame(cubey::host::WindowedAppContext& context,
                      const cubey::host::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        record_water_3d_compute(render_frame.command_buffer, resources_, water_config_,
                                runtime_state_, render_frame.frame_slot, paused_, reset_requested_,
                                frame);
        if (is_water_3d_surface_view(render_view_)) {
            record_water_3d_surface_draw(
                render_frame.command_buffer, context.device(), surface_graph_executor_, resources_,
                water_config_, render_frame.frame_slot, runtime_state_, render_view_,
                render_camera(render_frame.color_target.extent), render_frame.color_target,
                Water3DRenderTargetMode::Present, ibl_environment());
        } else {
            cubey::render::record_present_render_target(
                recorder, cubey::render::render_target_view(render_frame.color_target),
                [this, &render_frame](const cubey::vulkan::CommandRecorder& present_recorder) {
                    record_water_3d_draw(present_recorder.handle(), resources_, water_config_,
                                         render_frame.frame_slot, runtime_state_, render_view_,
                                         render_camera(render_frame.color_target.extent),
                                         render_frame.color_target);
                });
        }
        recorder.end("vkEndCommandBuffer water_3d");
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          cubey::render::FrameSlot frame_slot,
                                          const ProjectFrame& frame) {
        static_cast<void>(gpu.submit_and_wait({
            .label = "water_3d headless simulation frame",
            .work =
                [this, frame_slot, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    record_water_3d_compute(commands.command_buffer(), resources_, water_config_,
                                            runtime_state_, frame_slot, paused_, reset_requested_,
                                            frame);
                    commands.submit_and_wait();
                },
        }));
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
        if (config_.capture_mode == CaptureMode::Png) {
            callbacks.before_capture = [this](cubey::host::HeadlessPngContext&) {
                const std::uint32_t frames = water_3d_headless_frame_count(config_);
                for (std::uint32_t frame = 1; frame <= frames; ++frame) {
                    const cubey::render::FrameSlot frame_slot = cubey::render::frame_slot_for_index(
                        frame - 1U, cubey::host::headless_capture_frame_slot_count(config_));
                    const ProjectFrame project_frame = runtime_.frame_for_timing(
                        fixed_water_3d_headless_timing(water_config_, frame));
                    record_headless_simulation_frame(runtime_.gpu(), frame_slot, project_frame);
                }
            };
        } else {
            callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                            const cubey::host::HeadlessCaptureFrame& frame) {
                const std::uint64_t simulation_frame = static_cast<std::uint64_t>(frame.index) + 1;
                const FrameTiming timing{
                    .delta_seconds = frame.timing.delta_seconds,
                    .elapsed_seconds =
                        frame.timing.delta_seconds * static_cast<double>(simulation_frame),
                    .frame_index = simulation_frame,
                };
                const ProjectFrame project_frame = runtime_.frame_for_timing(timing);
                record_headless_simulation_frame(runtime_.gpu(), frame.frame_slot, project_frame);
            };
        }
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
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_water_3d(const RunConfig& config, Water3DAppInfo app_info) {
    Water3DApp app(config, app_info);
    return app.run();
}

} // namespace cubey::projects::fluid::water_3d
