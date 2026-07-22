#include "water_2d_app.h"

#include "water_2d_commands.h"
#include "water_2d_config.h"
#include "water_2d_diagnostics.h"
#include "water_2d_gpu_resources.h"
#include "water_2d_ui.h"

#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/immediate_commands.h>

#include <vulkan/vulkan.h>

#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::water_2d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

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
        const auto input = context.filtered_input();
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
        draw_water_2d_ui({
            .title = "Water 2D",
            .config = water_config_,
            .runtime_state = runtime_state_,
            .resources = resources_,
            .performance =
                {
                    .frame_stats = latest_frame_stats_,
                    .latest_fps = latest_fps_,
                    .latest_frame_ms = latest_frame_ms_,
                    .process = process_stats_.sample(),
                    .device_memory_budget = context.device().device_memory_budget(),
                },
            .debug_view = debug_view_,
            .paused = paused_,
            .reset_requested = reset_requested_,
        });
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        resources_.destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        graph_executor_.clear();
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

    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent) {
        resources_.create_render_pipeline(device, color_format, extent);
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

        const Water2DFrameGraph frame_graph = build_water_2d_frame_graph(
            render_frame.color_target, resources_, water_config_, runtime_state_,
            render_frame.frame_slot, debug_view_, paused_, reset_requested_, frame,
            Water2DRenderTargetMode::Present, true);
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (profiler != nullptr) {
            profiler->begin_frame(render_frame.command_buffer, render_frame.frame_slot.index);
        }
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &context.device(),
                .command_buffer = render_frame.command_buffer,
                .frame_slot = render_frame.frame_slot,
                .label = "vkEndCommandBuffer water_2d",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                .profiler = profiler,
            },
            frame_graph.graph,
            [this, &context, &frame_graph, frame_slot = render_frame.frame_slot](
                const cubey::render::RenderGraphResourceSet& graph_resources) {
                update_surface_descriptors(context.device(), frame_slot, frame_graph,
                                           graph_resources);
            });
        recorder.end("vkEndCommandBuffer water_2d");
    }

    void update_surface_descriptors(const cubey::vulkan::Device& device,
                                    cubey::render::FrameSlot frame_slot,
                                    const Water2DFrameGraph& frame_graph,
                                    const cubey::render::RenderGraphResourceSet& graph_resources) {
        if (!frame_graph.uses_surface_textures) {
            return;
        }
        resources_.update_surface_descriptors(
            device, frame_slot,
            cubey::render::resolved_sampled_texture_view(frame_graph.graph, graph_resources,
                                                         frame_graph.raw_density),
            cubey::render::resolved_sampled_texture_view(frame_graph.graph, graph_resources,
                                                         frame_graph.surface_a),
            cubey::render::resolved_sampled_texture_view(frame_graph.graph, graph_resources,
                                                         frame_graph.surface_b),
            cubey::render::resolved_sampled_texture_view(frame_graph.graph, graph_resources,
                                                         frame_graph.final_surface));
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
        std::printf("water_2d_gpu:");
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
            .label = "water_2d headless simulation frame",
            .work =
                [this, frame_slot, frame, profile_recorder,
                 frame_index](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
                    if (profiler != nullptr) {
                        profiler->begin_frame(commands.command_buffer(), frame_slot.index);
                    }
                    {
                        cubey::vulkan::GpuTimestampScope profile_scope(
                            profiler, commands.command_buffer(), frame_slot.index,
                            "water simulation");
                        record_water_2d_compute(commands.command_buffer(), resources_,
                                                water_config_, runtime_state_, frame_slot, paused_,
                                                reset_requested_, frame);
                    }
                    commands.submit_and_wait();
                    if (profiler != nullptr) {
                        profiler->collect(frame_slot.index);
                        record_gpu_timings(profile_recorder, frame_index,
                                           resources_.latest_timings());
                    }
                },
        }));
        if (should_record_water_2d_diagnostics(profile_recorder, water_config_, frame_index)) {
            const std::vector<std::uint8_t> diagnostics = gpu.readback_buffer(
                resources_.diagnostics().handle(), resources_.diagnostics().size(),
                "water_2d diagnostics readback");
            record_water_2d_diagnostics(*profile_recorder, frame_index, water_config_, diagnostics);
        }
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            const std::uint32_t frame_slot_count =
                cubey::host::headless_capture_frame_slot_count(config_);
            create_global_resources_if_needed(context.device(), context.gpu(), frame_slot_count);
            create_render_pipeline(context.device(), target.format, target.extent);
            graph_executor_.clear();
            graph_executor_.resize(frame_slot_count);
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
            if (debug_view_ == Water2DDebugView::Surface) {
                const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
                const Water2DFrameGraph frame_graph = build_water_2d_frame_graph(
                    target, resources_, water_config_, runtime_state_, frame.frame_slot,
                    debug_view_, paused_, reset_requested_, project_frame,
                    Water2DRenderTargetMode::ColorAttachment, false);
                graph_executor_.record(
                    cubey::render::RenderGraphFrameRecordInfo{
                        .device = &context.device(),
                        .command_buffer = command_buffer,
                        .frame_slot = frame.frame_slot,
                        .label = "vkEndCommandBuffer water_2d headless surface",
                        .command_buffer_mode =
                            cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                    },
                    frame_graph.graph,
                    [this, &context, &frame_graph, frame_slot = frame.frame_slot](
                        const cubey::render::RenderGraphResourceSet& graph_resources) {
                        update_surface_descriptors(context.device(), frame_slot, frame_graph,
                                                   graph_resources);
                    });
            } else {
                record_water_2d_draw(command_buffer, resources_, water_config_, frame.frame_slot,
                                     runtime_state_, debug_view_, target);
            }
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_all_resources();
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
    cubey::host::ProcessResourceStatsSampler process_stats_;
    Water2DDebugView debug_view_ = Water2DDebugView::Surface;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    double last_gpu_timing_print_seconds_ = -1.0;
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_water_2d(const RunConfig& config) {
    Water2DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid::water_2d
