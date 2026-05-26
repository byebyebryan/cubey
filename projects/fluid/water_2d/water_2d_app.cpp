#include "water_2d_app.h"

#include "water_2d_commands.h"
#include "water_2d_config.h"
#include "water_2d_gpu_resources.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace cubey::projects::fluid::water_2d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr std::array<Water2DDebugView, 8> kDebugViews{
    Water2DDebugView::Surface,  Water2DDebugView::Particles,  Water2DDebugView::Cells,
    Water2DDebugView::Velocity, Water2DDebugView::Divergence, Water2DDebugView::Pressure,
    Water2DDebugView::Solid,    Water2DDebugView::Foam,
};

constexpr std::array<Water2DScenario, 4> kScenarios{
    Water2DScenario::DamBreak,
    Water2DScenario::ObstacleSplash,
    Water2DScenario::WaveSlab,
    Water2DScenario::HoseFill,
};

constexpr std::array<Water2DObstacleShape, 3> kObstacleShapes{
    Water2DObstacleShape::None,
    Water2DObstacleShape::Circle,
    Water2DObstacleShape::Box,
};

constexpr std::array<Water2DTransferMode, 2> kTransferModes{
    Water2DTransferMode::Apic,
    Water2DTransferMode::PicFlip,
};

[[nodiscard]] double bytes_to_mib(VkDeviceSize bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

class Water2DApp {
  public:
    explicit Water2DApp(RunConfig config)
        : config_(std::move(config)), runtime_(1),
          water_config_(water_2d_config_from_run_config(config_)),
          debug_view_(water_2d_debug_view_from_name(config_.debug_view)) {}

    Water2DApp(const Water2DApp&) = delete;
    Water2DApp& operator=(const Water2DApp&) = delete;

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
            graph_executor_.clear();
            graph_executor_.resize(context.frame_slot_count());
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
                .app_name = "water_2d",
                .ready_status = "rendering 2D water project",
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
        (void)project_frame;
        const cubey::input::InputFrame& input = context.input();
        if (!context.ui_wants_keyboard()) {
            if (input.key_pressed(cubey::input::Key::Space)) {
                paused_ = !paused_;
            }
            if (input.key_pressed(cubey::input::Key::R)) {
                reset_requested_ = true;
            }
            if (input.key_pressed(cubey::input::Key::D)) {
                debug_view_ = next_debug_view(debug_view_);
            }
        }
    }

    std::optional<FrameStatsSample> record_frame_stats(cubey::host::WindowedAppContext& context,
                                                       const FrameTiming& timing) {
        const VkExtent2D extent = context.swapchain().extent();
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = 1,
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
        if (!ImGui::Begin("Water 2D")) {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Paused", &paused_);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            reset_requested_ = true;
        }

        if (ImGui::BeginCombo("Debug view", water_2d_debug_view_name(debug_view_))) {
            for (Water2DDebugView view : kDebugViews) {
                const bool selected = view == debug_view_;
                if (ImGui::Selectable(water_2d_debug_view_name(view), selected)) {
                    debug_view_ = view;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Scenario", water_2d_scenario_name(water_config_.scenario))) {
            for (Water2DScenario scenario : kScenarios) {
                const bool selected = scenario == water_config_.scenario;
                if (ImGui::Selectable(water_2d_scenario_name(scenario), selected)) {
                    water_config_.scenario = scenario;
                    apply_water_2d_scenario_defaults(water_config_);
                    reset_requested_ = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Transfer",
                              water_2d_transfer_mode_name(water_config_.transfer_mode))) {
            for (Water2DTransferMode mode : kTransferModes) {
                const bool selected = mode == water_config_.transfer_mode;
                if (ImGui::Selectable(water_2d_transfer_mode_name(mode), selected)) {
                    water_config_.transfer_mode = mode;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        int pressure_iterations = static_cast<int>(water_config_.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 512)) {
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
        ImGui::SliderFloat("Particle separation radius", &water_config_.particle_separation_radius,
                           0.20F, 1.40F, "%.2f");
        ImGui::SliderFloat("Particle separation strength",
                           &water_config_.particle_separation_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Particle volume strength", &water_config_.particle_volume_strength,
                           0.0F, 48.0F, "%.1f");
        int transfer_limit = static_cast<int>(water_config_.max_particles_per_cell);
        if (ImGui::SliderInt("Transfer limit/cell", &transfer_limit, 8, 256)) {
            water_config_.max_particles_per_cell = static_cast<std::uint32_t>(transfer_limit);
        }
        ImGui::SliderFloat("Particle radius", &water_config_.particle_radius, 0.0025F, 0.025F,
                           "%.4f");
        ImGui::SliderFloat("Gravity", &water_config_.gravity, -4.0F, 0.0F, "%.2f");
        if (ImGui::SliderFloat("Fill height", &water_config_.initial_fill_height,
                               kWater2DMinFillFraction, kWater2DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(water_config_);
            reset_requested_ = true;
        }
        if (ImGui::SliderFloat("Fill width", &water_config_.initial_fill_width,
                               kWater2DMinFillFraction, kWater2DMaxFillFraction, "%.2f")) {
            refresh_particle_counts(water_config_);
            reset_requested_ = true;
        }

        ImGui::Checkbox("Hose", &water_config_.hose.enabled);
        ImGui::SliderFloat2("Hose position", water_config_.hose.position.data(), 0.04F, 0.96F,
                            "%.2f");
        ImGui::SliderFloat("Hose angle", &water_config_.hose.angle_degrees, -90.0F, 20.0F, "%.1f");
        ImGui::SliderFloat("Hose speed", &water_config_.hose.speed, 0.2F, 5.0F, "%.2f");
        ImGui::SliderFloat("Hose radius", &water_config_.hose.radius, 0.005F, 0.080F, "%.3f");
        ImGui::SliderFloat("Hose rate", &water_config_.hose.particles_per_second, 0.0F, 60000.0F,
                           "%.0f");
        ImGui::SliderFloat("Hose spread", &water_config_.hose.spread_degrees, 0.0F, 45.0F, "%.1f");

        ImGui::Checkbox("Drain", &water_config_.drain.enabled);
        ImGui::SliderFloat2("Drain center", water_config_.drain.center.data(), 0.04F, 0.96F,
                            "%.2f");
        ImGui::SliderFloat2("Drain half size", water_config_.drain.half_size.data(), 0.01F, 0.24F,
                            "%.3f");

        if (ImGui::BeginCombo("Obstacle",
                              water_2d_obstacle_shape_name(water_config_.obstacle_shape))) {
            for (Water2DObstacleShape shape : kObstacleShapes) {
                const bool selected = shape == water_config_.obstacle_shape;
                if (ImGui::Selectable(water_2d_obstacle_shape_name(shape), selected)) {
                    water_config_.obstacle_shape = shape;
                    reset_requested_ = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (water_config_.obstacle_shape != Water2DObstacleShape::None) {
            if (ImGui::SliderFloat2("Obstacle center", water_config_.obstacle_center.data(), 0.10F,
                                    0.90F, "%.2f")) {
                reset_requested_ = true;
            }
        }
        if (water_config_.obstacle_shape == Water2DObstacleShape::Circle) {
            if (ImGui::SliderFloat("Obstacle radius", &water_config_.obstacle_radius, 0.02F, 0.22F,
                                   "%.3f")) {
                reset_requested_ = true;
            }
        }
        if (water_config_.obstacle_shape == Water2DObstacleShape::Box) {
            if (ImGui::SliderFloat2("Obstacle half size", water_config_.obstacle_half_size.data(),
                                    0.02F, 0.24F, "%.3f")) {
                reset_requested_ = true;
            }
        }
        ImGui::SliderFloat("Boundary bounce", &water_config_.boundary_restitution, 0.0F, 0.8F,
                           "%.2f");
        ImGui::SliderFloat("Obstacle friction", &water_config_.obstacle_friction, 0.0F, 1.0F,
                           "%.2f");
        ImGui::SliderFloat("Surface threshold", &water_config_.surface_threshold, 0.20F, 1.60F,
                           "%.2f");
        ImGui::SliderFloat("Edge strength", &water_config_.edge_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Foam strength", &water_config_.foam_strength, 0.0F, 1.5F, "%.2f");

        ImGui::Text("Grid: %u x %u", water_config_.grid_width, water_config_.grid_height);
        const std::uint32_t hose_pool_capacity =
            hose_particle_pool_capacity_for_config(water_config_);
        const std::uint32_t scanned_particles =
            water_2d_runtime_particle_scan_count(water_config_, runtime_state_);
        const std::uint32_t touched_hose_particles =
            scanned_particles > water_config_.active_particle_count
                ? scanned_particles - water_config_.active_particle_count
                : 0U;
        ImGui::Text("Particles: %u reset / %u hose pool / %u total",
                    water_config_.active_particle_count, hose_pool_capacity,
                    water_config_.particle_capacity);
        ImGui::Text("Compute particles: %u scanned / %u total", scanned_particles,
                    water_config_.particle_capacity);
        ImGui::Text("Hose touched: %u / %u", touched_hose_particles, hose_pool_capacity);
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

    void destroy_swapchain_resources() {
        graph_executor_.clear();
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
        const cubey::render::CompiledRenderGraph frame_graph = build_water_2d_frame_graph(
            render_frame.color_target, resources_, water_config_, runtime_state_,
            render_frame.frame_slot, debug_view_, paused_, reset_requested_, frame);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &context.device(),
                .command_buffer = render_frame.command_buffer,
                .frame_slot = render_frame.frame_slot,
                .label = "vkEndCommandBuffer water_2d",
            },
            frame_graph);
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          cubey::render::FrameSlot frame_slot,
                                          const ProjectFrame& frame) {
        static_cast<void>(gpu.submit_and_wait({
            .label = "water_2d headless simulation frame",
            .work =
                [this, frame_slot, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    record_water_2d_compute(commands.command_buffer(), resources_, water_config_,
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
        cubey::host::install_headless_simulation_driver(
            callbacks, config_,
            {
                .png_frame_count = water_2d_headless_frame_count(config_),
                .png_timing =
                    [this](std::uint64_t simulation_frame) {
                        return fixed_water_2d_headless_timing(water_config_, simulation_frame);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext&,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
                        record_headless_simulation_frame(runtime_.gpu(), frame.frame_slot,
                                                         project_frame);
                    },
            });
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext&,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            record_water_2d_draw(command_buffer, resources_, water_config_, frame.frame_slot,
                                 runtime_state_, debug_view_, target);
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
    cubey::ProjectRuntimeAdapter runtime_;
    Water2DConfig water_config_;
    Water2DRuntimeState runtime_state_;
    Water2DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::host::FrameStats ui_frame_stats_{0.25};
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    Water2DDebugView debug_view_ = Water2DDebugView::Surface;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_water_2d(const RunConfig& config) {
    Water2DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid::water_2d
