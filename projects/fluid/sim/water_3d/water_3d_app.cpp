#include "water_3d_app.h"

#include "water_3d_commands.h"
#include "water_3d_config.h"
#include "water_3d_gpu_resources.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/pass.h>
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
#include <optional>
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

constexpr std::array<Water3DDebugView, 6> kDebugViews{
    Water3DDebugView::Particles, Water3DDebugView::Cells, Water3DDebugView::Velocity,
    Water3DDebugView::Pressure,  Water3DDebugView::Solid, Water3DDebugView::Overpack,
};

constexpr std::array<Water3DTransferMode, 2> kTransferModes{
    Water3DTransferMode::Apic,
    Water3DTransferMode::PicFlip,
};

[[nodiscard]] double bytes_to_mib(VkDeviceSize bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

class Water3DApp {
  public:
    Water3DApp(RunConfig config, Water3DAppInfo app_info)
        : config_(std::move(config)), app_info_(app_info), runtime_(1),
          water_config_(water_3d_config_from_run_config(config_)),
          debug_view_(water_3d_debug_view_from_name(config_.debug_view)) {
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
                debug_view_ = next_debug_view(debug_view_);
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
            .triangles = debug_view_ == Water3DDebugView::Particles ? particle_scan_count * 2U
                                                                     : 2U,
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

        if (ImGui::BeginCombo("Debug view", water_3d_debug_view_name(debug_view_))) {
            for (Water3DDebugView view : kDebugViews) {
                const bool selected = view == debug_view_;
                if (ImGui::Selectable(water_3d_debug_view_name(view), selected)) {
                    debug_view_ = view;
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
        const cubey::Transform3D transform =
            cubey::orbit_camera_transform(cubey::OrbitCameraState{
                .target = kVolumeCenter,
                .distance = orbit_controller_.distance(),
                .yaw = kCameraBaseYaw + orbit_controller_.yaw(),
                .pitch = kCameraBasePitch + orbit_controller_.pitch(),
            });
        const cubey::math::Quat rotation = transform.rotation;
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .right = rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F},
            .up = rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F},
        };
    }

    void destroy_swapchain_resources() {
        resources_.destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        resources_.destroy_all_resources();
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        attach_project_gpu(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), water_config_,
                                                     frame_slot_count);
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
        resources_.create_render_pipeline(device, color_format, extent);
    }

    void record_frame(cubey::host::WindowedAppContext& context,
                      const cubey::host::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        (void)context;
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        record_water_3d_compute(render_frame.command_buffer, resources_, water_config_,
                                runtime_state_, render_frame.frame_slot, paused_,
                                reset_requested_, frame);
        cubey::render::record_present_render_target(
            recorder, cubey::render::render_target_view(render_frame.color_target),
            [this, &render_frame](const cubey::vulkan::CommandRecorder& present_recorder) {
                record_water_3d_draw(present_recorder.handle(), resources_, water_config_,
                                     render_frame.frame_slot, runtime_state_, debug_view_,
                                     render_camera(render_frame.color_target.extent),
                                     render_frame.color_target);
            });
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
                        frame - 1U,
                        cubey::host::headless_capture_frame_slot_count(config_));
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
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext&,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            record_water_3d_draw(command_buffer, resources_, water_config_, frame.frame_slot,
                                 runtime_state_, debug_view_, render_camera(target.extent),
                                 target);
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
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_{0.25};
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    Water3DDebugView debug_view_ = Water3DDebugView::Particles;
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
