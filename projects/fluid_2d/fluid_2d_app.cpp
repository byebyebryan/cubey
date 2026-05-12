#include "fluid_2d_app.h"

#include "fluid_2d_commands.h"
#include "fluid_2d_config.h"
#include "fluid_2d_gpu_resources.h"

#include <cubey/app/glfw_window.h>
#include <cubey/app/windowed_host.h>
#include <cubey/core/frame_stats.h>
#include <cubey/input/pointer_drag.h>
#include <cubey/runtime/headless_png_host.h>
#include <cubey/runtime/project_gpu_services.h>
#include <cubey/runtime/project_runtime.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>

namespace cubey::projects::fluid_2d {
namespace {

using cubey::FrameStatsSample;
using cubey::FrameTiming;
using cubey::ProjectFrame;

class Fluid2DApp {
  public:
    explicit Fluid2DApp(RunConfig config) : config_(std::move(config)), runtime_(1) {}

    Fluid2DApp(const Fluid2DApp&) = delete;
    Fluid2DApp& operator=(const Fluid2DApp&) = delete;

    int run() {
        if (config_.headless) {
            return run_headless();
        }

        cubey::app::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            {
                .create_swapchain_resources =
                    [this](cubey::app::WindowedAppContext& context) {
                        create_global_resources_if_needed(context.device(), context.gpu());
                        create_render_pipeline(context.device(), context.swapchain().format(),
                                               context.swapchain().extent());
                    },
                .destroy_swapchain_resources =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
                .on_ready =
                    [](cubey::app::WindowedAppContext& context) {
                        std::printf("fluid_2d: %s rendering 2D fluid project at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update =
                    [this](cubey::app::WindowedAppContext& context, const FrameTiming& timing) {
                        const ProjectFrame& project_frame = runtime_.frame_for_timing(timing);
                        update_interaction(context, project_frame);
                    },
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context,
                           const cubey::app::WindowedRenderFrame& frame) {
                        (void)context;
                        const ProjectFrame& project_frame = runtime_.frame_for_timing(frame.timing);
                        record_frame(frame, project_frame);
                    },
                .frame_stats_sample =
                    [](cubey::app::WindowedAppContext& context,
                       const FrameTiming& timing) -> std::optional<FrameStatsSample> {
                    const VkExtent2D extent = context.swapchain().extent();
                    return FrameStatsSample{
                        .delta_seconds = timing.delta_seconds,
                        .width = extent.width,
                        .height = extent.height,
                        .triangles = 1,
                    };
                },
                .shutdown =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_all_resources();
                        retire_project_gpu_work();
                        detach_project_gpu();
                    },
            });
        return host.run();
    }

  private:
    void update_interaction(cubey::app::WindowedAppContext& context,
                            const ProjectFrame& project_frame) {
        (void)project_frame;
        const cubey::input::InputFrame& input = context.input();
        if (input.key_pressed(cubey::input::Key::Escape)) {
            context.window().request_close();
        }
        if (input.key_pressed(cubey::input::Key::Space)) {
            paused_ = !paused_;
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_requested_ = true;
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            debug_view_ = next_debug_view(debug_view_);
        }

        pointer_drag_.update(input);
        const VkExtent2D extent = context.swapchain().extent();
        frame_injection_ = {};
        if (!pointer_drag_.active() || !pointer_drag_.has_cursor() || extent.width == 0 ||
            extent.height == 0) {
            static_cast<void>(pointer_drag_.consume_accumulated_delta());
            return;
        }

        const float width = static_cast<float>(extent.width);
        const float height = static_cast<float>(extent.height);
        const cubey::input::CursorPosition cursor = pointer_drag_.cursor();
        const cubey::input::PointerDelta delta = pointer_drag_.consume_accumulated_delta();
        const float cursor_x = static_cast<float>(cursor.x);
        const float cursor_y = static_cast<float>(cursor.y);
        const float delta_x = static_cast<float>(delta.x);
        const float delta_y = static_cast<float>(delta.y);

        frame_injection_ = {
            .active = true,
            .xy =
                {
                    std::clamp(cursor_x / width, 0.0F, 1.0F),
                    std::clamp(1.0F - (cursor_y / height), 0.0F, 1.0F),
                },
            .force =
                {
                    std::clamp((delta_x / width) * 90.0F, -8.0F, 8.0F),
                    std::clamp((-delta_y / height) * 90.0F, -8.0F, 8.0F),
                },
        };
    }

    void destroy_swapchain_resources() {
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

    void record_frame(const cubey::app::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        record_fluid_frame(render_frame, resources_, fluid_config_, debug_view_, frame_injection_,
                           paused_, reset_requested_, frame);
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const ProjectFrame& frame) {
        static_cast<void>(gpu.submit_and_wait({
            .label = "fluid_2d headless simulation frame",
            .work =
                [this, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    record_fluid_compute(commands.command_buffer(), resources_, fluid_config_,
                                         frame_injection_, paused_, reset_requested_, frame);
                    commands.submit_and_wait();
                },
        }));
    }

    int run_headless() {
        cubey::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::HeadlessPngContext& context) {
            const cubey::HeadlessRenderTarget& target = context.render_target();
            create_global_resources_if_needed(context.device(), context.gpu());
            create_render_pipeline(context.device(), target.format, target.extent);
        };
        callbacks.before_capture = [this](cubey::HeadlessPngContext&) {
            const std::uint32_t frames = headless_frame_count(config_);
            for (std::uint32_t frame = 1; frame <= frames; ++frame) {
                const ProjectFrame project_frame =
                    runtime_.frame_for_timing(fixed_headless_timing(fluid_config_, frame));
                record_headless_simulation_frame(runtime_.gpu(), project_frame);
            }
        };
        callbacks.record_capture = [this](cubey::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::HeadlessRenderTarget& target) {
            record_fullscreen_draw(command_buffer, resources_, fluid_config_, debug_view_,
                                   target.view, target.extent);
        };
        callbacks.shutdown = [this](cubey::HeadlessPngContext&) {
            destroy_all_resources();
            retire_project_gpu_work();
            detach_project_gpu();
        };

        cubey::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    RunConfig config_;
    cubey::ProjectRuntimeAdapter runtime_;
    Fluid2DConfig fluid_config_;
    cubey::input::PointerDrag pointer_drag_;
    FrameInjection frame_injection_;
    Fluid2DGpuResources resources_;
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
