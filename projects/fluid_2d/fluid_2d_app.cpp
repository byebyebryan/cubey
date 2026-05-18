#include "fluid_2d_app.h"

#include "fluid_2d_commands.h"
#include "fluid_2d_config.h"
#include "fluid_2d_gpu_resources.h"
#include "fluid_2d_interaction.h"
#include "fluid_2d_injectors.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/pointer_drag.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cubey::projects::fluid_2d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;

[[nodiscard]] const char* debug_view_name(FluidDebugView view) {
    switch (view) {
    case FluidDebugView::Dye:
        return "Dye";
    case FluidDebugView::Velocity:
        return "Velocity";
    case FluidDebugView::Divergence:
        return "Divergence";
    case FluidDebugView::Pressure:
        return "Pressure";
    case FluidDebugView::Speed:
        return "Speed";
    case FluidDebugView::Vorticity:
        return "Vorticity";
    case FluidDebugView::Obstacle:
        return "Obstacle";
    }
    return "Dye";
}

[[nodiscard]] const char* injector_motion_name(Fluid2DInjectorMotion motion) {
    switch (motion) {
    case Fluid2DInjectorMotion::OneRing:
        return "One ring";
    case Fluid2DInjectorMotion::TwoRings:
        return "Two rings";
    case Fluid2DInjectorMotion::RandomOrbit:
        return "Random orbit";
    case Fluid2DInjectorMotion::Lissajous:
        return "Lissajous";
    }
    return "Two rings";
}

constexpr std::array<FluidDebugView, 7> kDebugViews{
    FluidDebugView::Dye,      FluidDebugView::Velocity,  FluidDebugView::Divergence,
    FluidDebugView::Pressure, FluidDebugView::Speed,     FluidDebugView::Vorticity,
    FluidDebugView::Obstacle,
};

constexpr std::array<Fluid2DInjectorMotion, 4> kInjectorMotions{
    Fluid2DInjectorMotion::OneRing,
    Fluid2DInjectorMotion::TwoRings,
    Fluid2DInjectorMotion::RandomOrbit,
    Fluid2DInjectorMotion::Lissajous,
};

class Fluid2DApp {
  public:
    explicit Fluid2DApp(RunConfig config)
        : config_(std::move(config)), runtime_(1),
          fluid_config_(fluid_config_from_run_config(config_)),
          injector_states_(create_fluid_2d_injectors(fluid_config_)),
          injector_gpu_(fluid_2d_injectors_to_gpu(injector_states_, fluid_config_)) {}

    Fluid2DApp(const Fluid2DApp&) = delete;
    Fluid2DApp& operator=(const Fluid2DApp&) = delete;

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
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) {
            draw_ui(context);
        };
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
                .app_name = "fluid_2d",
                .ready_status = "rendering 2D fluid project",
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

        cubey::input::InputFrame ui_blocked_input;
        pointer_drag_.update(context.ui_wants_mouse() ? ui_blocked_input : input);
        if (!paused_) {
            update_injectors(project_frame);
        }
        const VkExtent2D extent = context.window().window_extent();
        frame_injection_ = {};
        if (!pointer_drag_.active() || !pointer_drag_.has_cursor() || extent.width == 0 ||
            extent.height == 0) {
            static_cast<void>(pointer_drag_.consume_accumulated_delta());
            return;
        }

        const cubey::input::CursorPosition cursor = pointer_drag_.cursor();
        const cubey::input::PointerDelta delta = pointer_drag_.consume_accumulated_delta();
        frame_injection_ = frame_injection_from_pointer(cursor, delta, extent);
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        (void)context;
        ImGui::SetNextWindowSize(ImVec2(320.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Fluid 2D")) {
            ImGui::End();
            return;
        }

        if (ImGui::Checkbox("Paused", &paused_)) {
            frame_injection_ = {};
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            reset_requested_ = true;
            reset_injectors();
        }

        if (ImGui::BeginCombo("Debug view", debug_view_name(debug_view_))) {
            for (FluidDebugView view : kDebugViews) {
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

        int injector_count = static_cast<int>(fluid_config_.procedural_injector_count);
        if (ImGui::SliderInt("Injectors", &injector_count, 1,
                             static_cast<int>(kMaxProceduralInjectorCount))) {
            fluid_config_.procedural_injector_count = static_cast<std::uint32_t>(injector_count);
            reset_injectors();
        }

        if (ImGui::BeginCombo("Injector motion",
                              injector_motion_name(fluid_config_.injector_motion))) {
            for (Fluid2DInjectorMotion motion : kInjectorMotions) {
                const bool selected = motion == fluid_config_.injector_motion;
                if (ImGui::Selectable(injector_motion_name(motion), selected)) {
                    fluid_config_.injector_motion = motion;
                    reset_injectors();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        int pressure_iterations = static_cast<int>(fluid_config_.pressure_iterations);
        if (ImGui::SliderInt("Pressure iterations", &pressure_iterations, 1, 96)) {
            fluid_config_.pressure_iterations = static_cast<std::uint32_t>(pressure_iterations);
        }
        ImGui::SliderFloat("Vorticity", &fluid_config_.vorticity_strength, 0.0F, 40.0F);
        ImGui::SliderFloat("Dye decay", &fluid_config_.dye_decay_per_second, 0.950F, 1.0F,
                           "%.4f");
        ImGui::SliderFloat("Velocity decay", &fluid_config_.velocity_decay_per_second, 0.950F,
                           1.0F, "%.4f");
        ImGui::SliderFloat("Pointer radius", &fluid_config_.pointer_injection_radius, 0.005F,
                           0.080F, "%.3f");
        ImGui::SliderFloat("Pointer strength", &fluid_config_.pointer_injection_strength, 0.0F,
                           40.0F, "%.1f");
        ImGui::SliderFloat("Injector radius", &fluid_config_.fallback_injection_radius, 0.005F,
                           0.080F, "%.3f");
        ImGui::SliderFloat("Injector strength", &fluid_config_.fallback_injection_strength, 0.0F,
                           20.0F, "%.1f");

        ImGui::Text("Grid: %u x %u", fluid_config_.grid_width, fluid_config_.grid_height);
        ImGui::End();
    }

    void reset_injectors() {
        injector_states_ = create_fluid_2d_injectors(fluid_config_);
        injector_gpu_ = fluid_2d_injectors_to_gpu(injector_states_, fluid_config_);
    }

    void update_injectors(const ProjectFrame& frame) {
        const FrameTiming timing{
            .delta_seconds = frame.delta_seconds,
            .elapsed_seconds = frame.elapsed_seconds,
            .frame_index = frame.frame_index,
        };
        injector_gpu_ = update_fluid_2d_injectors(injector_states_, fluid_config_, timing);
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
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), fluid_config_);
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
        const cubey::render::CompiledRenderGraph frame_graph = build_fluid_frame_graph(
            render_frame.color_target, resources_, fluid_config_, debug_view_, frame_injection_,
            paused_, reset_requested_, frame, injector_gpu_);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &context.device(),
                .command_buffer = render_frame.command_buffer,
                .frame_slot = render_frame.frame_slot,
                .label = "vkEndCommandBuffer fluid_2d",
            },
            frame_graph);
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const ProjectFrame& frame) {
        update_injectors(frame);
        static_cast<void>(gpu.submit_and_wait({
            .label = "fluid_2d headless simulation frame",
            .work =
                [this, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    record_fluid_compute(commands.command_buffer(), resources_, fluid_config_,
                                         frame_injection_, paused_, reset_requested_, frame,
                                         injector_gpu_);
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
        if (config_.capture_mode == CaptureMode::Png) {
            callbacks.before_capture = [this](cubey::host::HeadlessPngContext&) {
                const std::uint32_t frames = headless_frame_count(config_);
                for (std::uint32_t frame = 1; frame <= frames; ++frame) {
                    const ProjectFrame project_frame =
                        runtime_.frame_for_timing(fixed_headless_timing(fluid_config_, frame));
                    record_headless_simulation_frame(runtime_.gpu(), project_frame);
                }
            };
        } else {
            callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                            const cubey::host::HeadlessCaptureFrame& frame) {
                const std::uint64_t simulation_frame = static_cast<std::uint64_t>(frame.index) + 1;
                const FrameTiming timing{
                    .delta_seconds = frame.timing.delta_seconds,
                    .elapsed_seconds = frame.timing.delta_seconds *
                                       static_cast<double>(simulation_frame),
                    .frame_index = simulation_frame,
                };
                const ProjectFrame project_frame = runtime_.frame_for_timing(timing);
                record_headless_simulation_frame(runtime_.gpu(), project_frame);
            };
        }
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            record_fullscreen_draw(command_buffer, resources_, fluid_config_, debug_view_, target);
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
    Fluid2DConfig fluid_config_;
    cubey::input::PointerDrag pointer_drag_;
    FrameInjection frame_injection_;
    std::vector<Fluid2DInjectorState> injector_states_;
    std::vector<Fluid2DInjectorGpu> injector_gpu_;
    Fluid2DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    FluidDebugView debug_view_ = FluidDebugView::Dye;
    bool paused_ = false;
    bool reset_requested_ = false;
};

} // namespace

int run_fluid_2d(const RunConfig& config) {
    Fluid2DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid_2d
