#include "smoke_2d_app.h"

#include "smoke_2d_commands.h"
#include "smoke_2d_config.h"
#include "smoke_2d_gpu_resources.h"
#include "smoke_2d_injectors.h"

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
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::smoke_2d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;

[[nodiscard]] const char* debug_view_name(Smoke2DDebugView view) {
    switch (view) {
    case Smoke2DDebugView::Dye:
        return "Dye";
    case Smoke2DDebugView::Velocity:
        return "Velocity";
    case Smoke2DDebugView::Divergence:
        return "Divergence";
    case Smoke2DDebugView::Pressure:
        return "Pressure";
    case Smoke2DDebugView::Speed:
        return "Speed";
    case Smoke2DDebugView::Vorticity:
        return "Vorticity";
    case Smoke2DDebugView::Obstacle:
        return "Obstacle";
    }
    return "Dye";
}

[[nodiscard]] Smoke2DDebugView debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "dye") {
        return Smoke2DDebugView::Dye;
    }
    if (name == "velocity") {
        return Smoke2DDebugView::Velocity;
    }
    if (name == "divergence") {
        return Smoke2DDebugView::Divergence;
    }
    if (name == "pressure") {
        return Smoke2DDebugView::Pressure;
    }
    if (name == "speed") {
        return Smoke2DDebugView::Speed;
    }
    if (name == "vorticity") {
        return Smoke2DDebugView::Vorticity;
    }
    if (name == "obstacle") {
        return Smoke2DDebugView::Obstacle;
    }
    throw std::runtime_error("smoke 2D debug view must be dye, velocity, divergence, pressure, "
                             "speed, vorticity, or obstacle");
}

constexpr std::array<Smoke2DDebugView, 7> kDebugViews{
    Smoke2DDebugView::Dye,      Smoke2DDebugView::Velocity, Smoke2DDebugView::Divergence,
    Smoke2DDebugView::Pressure, Smoke2DDebugView::Speed,    Smoke2DDebugView::Vorticity,
    Smoke2DDebugView::Obstacle,
};

class Smoke2DApp {
  public:
    explicit Smoke2DApp(RunConfig config)
        : config_(std::move(config)), runtime_(1),
          smoke_config_(smoke_2d_config_from_run_config(config_)),
          injector_states_(create_smoke_2d_injectors(smoke_config_)),
          injector_gpu_(smoke_2d_injectors_to_gpu(injector_states_, smoke_config_)),
          debug_view_(debug_view_from_name(config_.debug_view)) {}

    Smoke2DApp(const Smoke2DApp&) = delete;
    Smoke2DApp& operator=(const Smoke2DApp&) = delete;

    int run() {
        if (config_.headless) {
            return run_headless();
        }

        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu());
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
                .app_name = "smoke_2d",
                .ready_status = "rendering 2D smoke project",
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
                reset_requested_ = true;
                reset_injectors();
            }
            if (input.key_pressed(cubey::input::Key::D)) {
                debug_view_ = next_debug_view(debug_view_);
            }
        }

        if (!paused_) {
            update_injectors(project_frame);
        }
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        (void)context;
        ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Smoke 2D")) {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Paused", &paused_);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            reset_requested_ = true;
            reset_injectors();
        }

        if (ImGui::BeginCombo("Debug view", debug_view_name(debug_view_))) {
            for (Smoke2DDebugView view : kDebugViews) {
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

        int injector_count = static_cast<int>(smoke_config_.procedural_injector_count);
        if (ImGui::SliderInt("Injectors", &injector_count, 1,
                             static_cast<int>(kMaxProceduralInjectorCount))) {
            smoke_config_.procedural_injector_count = static_cast<std::uint32_t>(injector_count);
            reset_injectors();
        }

        int pressure_iterations = static_cast<int>(smoke_config_.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 96)) {
            smoke_config_.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        ImGui::SliderFloat("Vorticity", &smoke_config_.vorticity_strength, 0.0F, 40.0F);
        ImGui::SliderFloat("Dye decay", &smoke_config_.dye_decay_per_second, 0.950F, 1.0F, "%.4f");
        ImGui::SliderFloat("Velocity decay", &smoke_config_.velocity_decay_per_second, 0.950F, 1.0F,
                           "%.4f");
        ImGui::SliderFloat("Injector radius", &smoke_config_.injector_injection_radius, 0.005F,
                           0.080F, "%.3f");
        ImGui::SliderFloat("Injection force", &smoke_config_.injector_injection_strength, 0.0F,
                           20.0F, "%.1f");
        ImGui::SliderFloat("Propulsion", &smoke_config_.injector_propulsion_strength, 0.0F, 3.0F,
                           "%.2f");
        ImGui::SliderFloat("Orbit radius", &smoke_config_.injector_orbit_radius, 0.04F, 0.42F,
                           "%.3f");
        ImGui::SliderFloat("Radius spread", &smoke_config_.injector_orbit_radius_spread, 0.0F,
                           0.50F, "%.3f");
        ImGui::SliderFloat("Angular speed", &smoke_config_.injector_orbit_angular_speed, -2.0F,
                           2.0F, "%.2f");
        ImGui::SliderFloat("Speed spread", &smoke_config_.injector_orbit_angular_speed_spread, 0.0F,
                           4.0F, "%.2f");
        ImGui::SliderFloat("Phase spread", &smoke_config_.injector_orbit_phase_spread, 0.0F, 1.0F,
                           "%.2f");

        ImGui::Text("Grid: %u x %u", smoke_config_.grid_width, smoke_config_.grid_height);
        ImGui::End();
    }

    void reset_injectors() {
        injector_states_ = create_smoke_2d_injectors(smoke_config_);
        injector_gpu_ = smoke_2d_injectors_to_gpu(injector_states_, smoke_config_);
    }

    void update_injectors(const ProjectFrame& frame) {
        const FrameTiming timing{
            .delta_seconds = frame.delta_seconds,
            .elapsed_seconds = frame.elapsed_seconds,
            .frame_index = frame.frame_index,
        };
        injector_gpu_ = update_smoke_2d_injectors(injector_states_, smoke_config_, timing);
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        resources_.destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        resources_.destroy_all_resources();
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu) {
        attach_project_gpu(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), smoke_config_);
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
        const cubey::render::CompiledRenderGraph frame_graph =
            build_smoke_frame_graph(render_frame.color_target, resources_, smoke_config_,
                                    debug_view_, paused_, reset_requested_, frame, injector_gpu_);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &context.device(),
                .command_buffer = render_frame.command_buffer,
                .frame_slot = render_frame.frame_slot,
                .label = "vkEndCommandBuffer smoke_2d",
            },
            frame_graph);
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const ProjectFrame& frame) {
        update_injectors(frame);
        static_cast<void>(gpu.submit_and_wait({
            .label = "smoke_2d headless simulation frame",
            .work =
                [this, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    record_smoke_compute(commands.command_buffer(), resources_, smoke_config_,
                                         paused_, reset_requested_, frame, injector_gpu_);
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
            create_global_resources_if_needed(context.device(), context.gpu());
            create_render_pipeline(context.device(), target.format, target.extent);
        };
        cubey::host::install_headless_simulation_driver(
            callbacks, config_,
            {
                .png_frame_count = headless_frame_count(config_),
                .png_timing =
                    [this](std::uint64_t simulation_frame) {
                        return fixed_headless_timing(smoke_config_, simulation_frame);
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
            record_fullscreen_draw(command_buffer, resources_, smoke_config_, debug_view_, target);
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
    Smoke2DConfig smoke_config_;
    std::vector<Smoke2DInjectorState> injector_states_;
    std::vector<Smoke2DInjectorGpu> injector_gpu_;
    Smoke2DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    Smoke2DDebugView debug_view_ = Smoke2DDebugView::Dye;
    bool paused_ = false;
    bool reset_requested_ = false;
};

} // namespace

int run_smoke_2d(const RunConfig& config) {
    Smoke2DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid::smoke_2d
