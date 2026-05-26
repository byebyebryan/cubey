#include "pyro_3d_app.h"

#include "pyro_3d_commands.h"
#include "pyro_3d_config.h"
#include "pyro_3d_gpu_resources.h"
#include "pyro_3d_sources.h"

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
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::pyro_3d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;

constexpr float kCameraDistance = 2.45F;
constexpr cubey::math::Vec3 kVolumeCenter{0.5F, 0.5F, 0.5F};

[[nodiscard]] const char* debug_view_name(Pyro3DDebugView view) {
    switch (view) {
    case Pyro3DDebugView::Smoke:
        return "Smoke";
    case Pyro3DDebugView::DensitySlice:
        return "Density slice";
    case Pyro3DDebugView::Velocity:
        return "Velocity";
    }
    return "Smoke";
}

[[nodiscard]] Pyro3DDebugView debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "smoke") {
        return Pyro3DDebugView::Smoke;
    }
    if (name == "density" || name == "density-slice") {
        return Pyro3DDebugView::DensitySlice;
    }
    if (name == "velocity") {
        return Pyro3DDebugView::Velocity;
    }
    throw std::runtime_error("pyro 3D debug view must be smoke, density-slice, or velocity");
}

constexpr std::array<Pyro3DDebugView, 3> kDebugViews{
    Pyro3DDebugView::Smoke,
    Pyro3DDebugView::DensitySlice,
    Pyro3DDebugView::Velocity,
};

class Pyro3DApp {
  public:
    Pyro3DApp(RunConfig config, Pyro3DAppInfo app_info)
        : config_(std::move(config)), app_info_(app_info), runtime_(1),
          pyro_config_(pyro_3d_config_from_run_config(config_, app_info_.mode)),
          source_states_(create_pyro_3d_sources(pyro_config_)),
          source_gpu_(pyro_3d_sources_to_gpu(source_states_, pyro_config_)),
          debug_view_(debug_view_from_name(config_.debug_view)) {
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_auto_rotation_speed(0.12F);
    }

    Pyro3DApp(const Pyro3DApp&) = delete;
    Pyro3DApp& operator=(const Pyro3DApp&) = delete;

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
            const VkExtent2D extent = context.swapchain().extent();
            create_render_pipeline(context.device(), context.swapchain().format(), extent);
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
            [](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            const VkExtent2D extent = context.swapchain().extent();
            return FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = extent.width,
                .height = extent.height,
                .triangles = 1,
            };
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
            debug_view_ = next_debug_view(debug_view_);
        }

        update_camera_input(context, project_frame.delta_seconds);
        if (!paused_) {
            update_sources(project_frame);
        }
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

    void draw_ui(cubey::host::WindowedAppContext& context) {
        (void)context;
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

        if (ImGui::BeginCombo("Debug view", debug_view_name(debug_view_))) {
            for (Pyro3DDebugView view : kDebugViews) {
                const bool selected = view == debug_view_;
                if (ImGui::Selectable(debug_view_name(view), selected)) {
                    debug_view_ = view;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Mode: %s", pyro_3d_mode_name(pyro_config_.mode));

        int source_count = static_cast<int>(pyro_config_.source_count);
        if (ImGui::SliderInt("Sources", &source_count, 1,
                             static_cast<int>(kMaxPyro3DSourceCount))) {
            pyro_config_.source_count = static_cast<std::uint32_t>(source_count);
            reset_sources();
        }

        int pressure_iterations = static_cast<int>(pyro_config_.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 48)) {
            pyro_config_.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        int raymarch_steps = static_cast<int>(pyro_config_.raymarch_steps);
        if (ImGui::SliderInt("Raymarch steps", &raymarch_steps, 24, 192)) {
            pyro_config_.raymarch_steps = static_cast<std::uint32_t>(raymarch_steps);
        }
        ImGui::SliderFloat("Density decay", &pyro_config_.density_decay_per_second, 0.960F, 1.0F,
                           "%.4f");
        ImGui::SliderFloat("Velocity decay", &pyro_config_.velocity_decay_per_second, 0.960F, 1.0F,
                           "%.4f");
        ImGui::SliderFloat("Source radius", &pyro_config_.source_radius, 0.020F, 0.180F, "%.3f");
        ImGui::SliderFloat("Smoke", &pyro_config_.source_smoke_amount, 0.0F, 16.0F, "%.2f");
        ImGui::SliderFloat("Heat", &pyro_config_.source_heat_amount, 0.0F, 8.0F, "%.2f");
        ImGui::SliderFloat(pyro_config_.mode == Pyro3DMode::Fire ? "Fuel" : "Flame",
                           &pyro_config_.source_flame_amount, 0.0F, 12.0F, "%.2f");
        ImGui::SliderFloat("Velocity force", &pyro_config_.source_velocity_strength, 0.0F, 16.0F,
                           "%.2f");
        ImGui::SliderFloat("Buoyancy", &pyro_config_.buoyancy_strength, -2.0F, 6.0F, "%.2f");
        if (pyro_config_.mode == Pyro3DMode::Explosion) {
            ImGui::SliderFloat("Explosion interval", &pyro_config_.explosion_interval_seconds,
                               0.25F, 8.0F, "%.2f");
            const float max_duration = std::min(pyro_config_.explosion_interval_seconds, 1.0F);
            pyro_config_.explosion_duration_seconds =
                std::min(pyro_config_.explosion_duration_seconds, max_duration);
            ImGui::SliderFloat("Explosion duration", &pyro_config_.explosion_duration_seconds,
                               0.016F, max_duration, "%.3f");
            ImGui::SliderFloat("Explosion boost", &pyro_config_.explosion_boost, 0.0F, 40.0F,
                               "%.2f");
        }
        if (pyro_config_.mode == Pyro3DMode::Fire) {
            ImGui::SliderFloat("Ignition", &pyro_config_.fire_ignition_temperature, 0.0F, 2.0F,
                               "%.2f");
            ImGui::SliderFloat("Burn rate", &pyro_config_.fire_burn_rate, 0.0F, 10.0F, "%.2f");
            ImGui::SliderFloat("Heat output", &pyro_config_.fire_heat_output, 0.0F, 8.0F, "%.2f");
            ImGui::SliderFloat("Soot yield", &pyro_config_.fire_soot_yield, 0.0F, 1.5F, "%.2f");
        }
        if (pyro_config_.mode == Pyro3DMode::Fire || pyro_config_.mode == Pyro3DMode::Explosion) {
            ImGui::SliderFloat("Expansion", &pyro_config_.fire_expansion, 0.0F, 4.0F, "%.2f");
            ImGui::SliderFloat("Flame cooling", &pyro_config_.fire_flame_cooling, 0.0F, 8.0F,
                               "%.2f");
            ImGui::SliderFloat("Shredding", &pyro_config_.fire_shredding, 0.0F, 8.0F, "%.2f");
            ImGui::SliderFloat("Turbulence", &pyro_config_.fire_turbulence, 0.0F, 3.0F, "%.2f");
        }
        ImGui::SliderFloat("Vorticity", &pyro_config_.vorticity_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Obstacle height", &pyro_config_.obstacle_center_height, 0.0F, 1.0F,
                           "%.2f");
        ImGui::SliderFloat("Obstacle radius", &pyro_config_.obstacle_radius, 0.0F,
                           kMaxPyro3DObstacleRadius, "%.3f");
        ImGui::SliderFloat("Absorption", &pyro_config_.absorption, 0.5F, 12.0F, "%.2f");
        ImGui::SliderFloat("Light", &pyro_config_.emission, 0.1F, 4.0F, "%.2f");
        ImGui::SliderFloat("Shadow", &pyro_config_.shadow_absorption, 0.0F, 96.0F, "%.2f");
        int shadow_steps = static_cast<int>(pyro_config_.shadow_steps);
        if (ImGui::SliderInt("Shadow steps", &shadow_steps, 8, 192)) {
            pyro_config_.shadow_steps = static_cast<std::uint32_t>(shadow_steps);
        }
        int shadow_update_interval = static_cast<int>(pyro_config_.shadow_update_interval);
        if (ImGui::SliderInt("Shadow interval", &shadow_update_interval, 1, 8)) {
            pyro_config_.shadow_update_interval =
                static_cast<std::uint32_t>(shadow_update_interval);
        }
        ImGui::SliderFloat("Ambient", &pyro_config_.ambient_light, 0.0F, 1.0F, "%.2f");
        ImGui::Text("Grid: %u x %u x %u", pyro_config_.grid_width, pyro_config_.grid_height,
                    pyro_config_.grid_depth);
        ImGui::Text("Shadow grid: %u x %u x %u", pyro_config_.shadow_grid_width,
                    pyro_config_.shadow_grid_height, pyro_config_.shadow_grid_depth);
        const std::vector<cubey::vulkan::GpuPassTiming>& timings = resources_.latest_timings();
        if (!timings.empty()) {
            ImGui::Separator();
            ImGui::Text("GPU timings");
            for (const cubey::vulkan::GpuPassTiming& timing : timings) {
                ImGui::Text("%s: %.3f ms", timing.label.c_str(), timing.milliseconds);
            }
        }
        ImGui::End();
    }

    void reset_sources() {
        source_states_ = create_pyro_3d_sources(pyro_config_);
        source_gpu_ = pyro_3d_sources_to_gpu(source_states_, pyro_config_);
    }

    void reset_simulation() {
        reset_sources();
        reset_requested_ = true;
        frame_state_.density_a_current = true;
        frame_state_.velocity_a_current = true;
        frame_state_.shadow_initialized = false;
        frame_state_.frames_since_shadow_update = 0;
    }

    void update_sources(const ProjectFrame& frame) {
        const FrameTiming timing{
            .delta_seconds = frame.delta_seconds,
            .elapsed_seconds = frame.elapsed_seconds,
            .frame_index = frame.frame_index,
        };
        source_gpu_ = update_pyro_3d_sources(source_states_, pyro_config_, timing);
    }

    [[nodiscard]] Pyro3DRenderCamera render_camera() const {
        const cubey::Transform3D transform = cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = kVolumeCenter,
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw(),
            .pitch = orbit_controller_.pitch(),
        });
        const cubey::math::Quat rotation = transform.rotation;
        return {
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
        resources_.destroy_all_resources();
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        attach_project_gpu(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), pyro_config_,
                                                     frame_slot_count);
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
        resources_.create_render_pipeline(device, color_format, extent);
    }

    void record_frame(cubey::host::WindowedAppContext& context,
                      const cubey::host::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        (void)context;
        cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
        if (profiler != nullptr) {
            profiler->collect(render_frame.frame_slot.index);
            maybe_print_gpu_timings(frame);
        }
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (profiler != nullptr) {
            profiler->begin_frame(render_frame.command_buffer, render_frame.frame_slot.index);
        }
        record_pyro_3d_compute(render_frame.command_buffer, resources_, pyro_config_, paused_,
                               reset_requested_, frame, source_gpu_, frame_state_, true, profiler,
                               render_frame.frame_slot.index);
        cubey::render::record_present_render_target(
            recorder, cubey::render::render_target_view(render_frame.color_target),
            [this, &render_frame,
             profiler](const cubey::vulkan::CommandRecorder& present_recorder) {
                record_pyro_3d_draw(present_recorder.handle(), resources_, pyro_config_,
                                    debug_view_, render_camera(), render_frame.color_target,
                                    frame_state_, profiler, render_frame.frame_slot.index);
            });
        recorder.end("vkEndCommandBuffer pyro_3d");
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
        std::printf("pyro_3d_gpu:");
        for (const cubey::vulkan::GpuPassTiming& timing : timings) {
            std::printf(" %s=%.3fms", timing.label.c_str(), timing.milliseconds);
        }
        std::printf("\n");
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const ProjectFrame& frame) {
        update_sources(frame);
        static_cast<void>(gpu.submit_and_wait({
            .label = "pyro_3d headless simulation frame",
            .work =
                [this, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    record_pyro_3d_compute(commands.command_buffer(), resources_, pyro_config_,
                                           paused_, reset_requested_, frame, source_gpu_,
                                           frame_state_);
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
            create_global_resources_if_needed(context.device(), context.gpu(), 1);
            create_render_pipeline(context.device(), target.format, target.extent);
        };
        cubey::host::install_headless_simulation_driver(
            callbacks, config_,
            {
                .png_frame_count = pyro_3d_headless_frame_count(config_),
                .png_timing =
                    [this](std::uint64_t simulation_frame) {
                        return fixed_pyro_3d_headless_timing(pyro_config_, simulation_frame);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext&,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
                        record_headless_simulation_frame(runtime_.gpu(), project_frame);
                    },
            });
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            record_pyro_3d_draw(command_buffer, resources_, pyro_config_, debug_view_,
                                render_camera(), target, frame_state_);
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
    Pyro3DAppInfo app_info_;
    cubey::ProjectRuntimeAdapter runtime_;
    Pyro3DConfig pyro_config_;
    std::vector<Pyro3DSourceState> source_states_;
    std::vector<Pyro3DSourceGpu> source_gpu_;
    Pyro3DGpuResources resources_;
    Pyro3DFrameState frame_state_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    Pyro3DDebugView debug_view_ = Pyro3DDebugView::Smoke;
    double last_gpu_timing_print_seconds_ = -1.0;
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_pyro_3d(const RunConfig& config, Pyro3DAppInfo app_info) {
    Pyro3DApp app(config, app_info);
    return app.run();
}

} // namespace cubey::projects::fluid::pyro_3d
