#include "fractal_app.h"

#include "fractal_view.h"

#include <cubey/host/glfw_window.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_host.h>
#include <cubey/input/pan_zoom_2d_controller.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/shader_bytecode.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_FRACTAL_SHADER_DIR
#error "CUBEY_FRACTAL_SHADER_DIR must be defined by the fractal CMake target"
#endif

namespace cubey::examples::fractal {
namespace {

using cubey::vulkan::vk_struct;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FRACTAL_SHADER_DIR) / filename;
}

class FractalApp {
  public:
    explicit FractalApp(RunConfig config) : config_(std::move(config)) {}

    FractalApp(const FractalApp&) = delete;
    FractalApp& operator=(const FractalApp&) = delete;

    ~FractalApp() {
        pipeline_.reset();
        pipeline_layout_.reset();
    }

    int run() {
        if (config_.headless) {
            return run_headless();
        }

        cubey::host::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            {
                .create_swapchain_resources =
                    [this](cubey::host::WindowedAppContext& context) {
                        create_pipeline(context.device(), context.swapchain().format(),
                                        context.swapchain().extent());
                    },
                .destroy_swapchain_resources =
                    [this](cubey::host::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
                .on_ready =
                    [](cubey::host::WindowedAppContext& context) {
                        std::printf("fractal: %s rendering fullscreen fractal at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update =
                    [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
                        (void)timing;
                        update_input(context);
                    },
                .record_frame =
                    [this](cubey::host::WindowedAppContext& context,
                           const cubey::host::WindowedRenderFrame& frame) {
                        (void)context;
                        record_fractal_frame(frame);
                    },
                .frame_stats_sample = {},
                .shutdown =
                    [this](cubey::host::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
            });
        return host.run();
    }

  private:
    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_pipeline(context.device(), target.format, target.extent);
        };
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            record_fractal_draw(command_buffer, target);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_input(cubey::host::WindowedAppContext& context) {
        if (context.input().key_pressed(cubey::input::Key::Escape)) {
            context.window().request_close();
        }
        if (context.input().key_pressed(cubey::input::Key::R)) {
            view_controller_.reset();
        }

        const VkExtent2D extent = context.swapchain().extent();
        view_controller_.update_from_input(context.input(), static_cast<float>(extent.width),
                                           static_cast<float>(extent.height));
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_layout_.reset();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::vulkan::read_spirv_file(shader_path("fractal.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::vulkan::read_spirv_file(shader_path("fractal.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(device, vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(device, fragment_code);

        const VkPipelineShaderStageCreateInfo vertex_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle());
        const VkPipelineShaderStageCreateInfo fragment_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader.handle());
        const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
            vertex_stage,
            fragment_stage,
        };

        const VkPushConstantRange push_constants{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(FractalPushConstants),
        };
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = std::span<const VkDescriptorSetLayout>{},
            .push_constants = std::span<const VkPushConstantRange>(&push_constants, 1),
        });
        pipeline_layout_.emplace(device, layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = pipeline_layout().handle();
        pipeline_config.extent = extent;
        pipeline_config.color_format = color_format;
        pipeline_config.shader_stages = shader_stages;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        pipeline_.emplace(device, pipeline_info.create_info());
    }

    [[nodiscard]] FractalPushConstants push_constants(VkExtent2D extent) const {
        return view_.push_constants(view_controller_.camera(), extent.width, extent.height);
    }

    void record_fractal_draw(VkCommandBuffer command_buffer,
                             const cubey::render::ColorTargetView& target) const {
        VkClearValue clear{};
        clear.color = {{0.015F, 0.018F, 0.026F, 1.0F}};
        const cubey::render::RenderTargetView render_target =
            cubey::render::render_target_view(target);
        cubey::render::RenderClearValues clear_values;
        clear_values.color = clear;
        const cubey::render::RenderTargetRenderingInfo rendering(render_target, clear_values);

        const FractalPushConstants constants = push_constants(target.extent);
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        recorder.begin_rendering(rendering.info());
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        recorder.push_constants(pipeline_layout().handle(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                constants);
        recorder.draw(3);
        recorder.end_rendering();
    }

    void record_fractal_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        recorder.transition_image_layout(
            cubey::vulkan::begin_color_attachment_transition(frame.color_target.image));

        record_fractal_draw(recorder.handle(), frame.color_target);

        recorder.transition_image_layout(
            cubey::vulkan::finish_color_attachment_for_present_transition(
                frame.color_target.image));

        recorder.end("vkEndCommandBuffer fractal");
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& pipeline_layout() const {
        if (!pipeline_layout_.has_value()) {
            throw std::runtime_error("pipeline layout is not initialized");
        }
        return pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::GraphicsPipeline& pipeline() const {
        if (!pipeline_.has_value()) {
            throw std::runtime_error("pipeline is not initialized");
        }
        return pipeline_.value();
    }

    RunConfig config_;
    FractalView view_;
    cubey::input::PanZoom2DController view_controller_{cubey::Camera2D({
        .center = {-0.5F, 0.0F},
        .scale = 1.35F,
    })};

    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
};

} // namespace

int run_fractal(const RunConfig& config) {
    FractalApp app(config);
    return app.run();
}

} // namespace cubey::examples::fractal
