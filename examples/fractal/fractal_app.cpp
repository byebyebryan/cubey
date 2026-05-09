#include "fractal_app.h"

#include "fractal_view.h"

#include <cubey/app/glfw_window.h>
#include <cubey/app/windowed_host.h>
#include <cubey/headless_png_host.h>
#include <cubey/pan_zoom_2d_controller.h>
#include <cubey/spirv_io.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/pipeline.h>
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

using cubey::vulkan::check;
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

        cubey::app::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            {
                .create_swapchain_resources =
                    [this](cubey::app::WindowedAppContext& context) {
                        create_pipeline(context.device(), context.swapchain().format(),
                                        context.swapchain().extent());
                    },
                .destroy_swapchain_resources =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
                .on_ready =
                    [](cubey::app::WindowedAppContext& context) {
                        std::printf("fractal: %s rendering fullscreen fractal at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update =
                    [this](cubey::app::WindowedAppContext& context, const FrameTiming& timing) {
                        (void)timing;
                        update_input(context);
                    },
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                           std::uint32_t image_index, const FrameTiming& timing) {
                        (void)timing;
                        record_fractal_frame(context, command_buffer, image_index);
                    },
                .frame_stats_sample = {},
                .shutdown =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
            });
        return host.run();
    }

  private:
    int run_headless() {
        cubey::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;

        cubey::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::HeadlessPngContext& context) {
            const cubey::HeadlessRenderTarget& target = context.render_target();
            create_pipeline(context.device(), target.format, target.extent);
        };
        callbacks.record_capture = [this](cubey::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::HeadlessRenderTarget& target) {
            record_fractal_draw(command_buffer, target.view, target.extent);
        };
        callbacks.shutdown = [this](cubey::HeadlessPngContext&) { destroy_swapchain_resources(); };

        cubey::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_input(cubey::app::WindowedAppContext& context) {
        if (context.input().key_pressed(cubey::input::Key::Escape)) {
            context.window().request_close();
        }
        if (context.input().key_pressed(cubey::input::Key::R)) {
            view_controller_.reset();
        }

        const VkExtent2D extent = context.swapchain().extent();
        view_controller_.update_from_input(context.input(), static_cast<float>(extent.width),
                                           static_cast<float>(extent.height));
        view_.set_view(view_controller_.center_x(), view_controller_.center_y(),
                       view_controller_.scale());
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_layout_.reset();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("fractal.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::read_spirv_file(shader_path("fractal.frag.spv"));
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
        return view_.push_constants(extent.width, extent.height);
    }

    void record_fractal_draw(VkCommandBuffer command_buffer, VkImageView image_view,
                             VkExtent2D extent) const {
        VkClearValue clear{};
        clear.color = {{0.015F, 0.018F, 0.026F, 1.0F}};
        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(image_view, clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;

        const FractalPushConstants constants = push_constants(extent);
        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        vkCmdPushConstants(command_buffer, pipeline_layout().handle(), VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(constants), &constants);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);
        vkCmdEndRendering(command_buffer);
    }

    void record_fractal_frame(cubey::app::WindowedAppContext& context,
                              VkCommandBuffer command_buffer, std::uint32_t image_index) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        cubey::vulkan::Swapchain& swapchain = context.swapchain();
        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain.images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        record_fractal_draw(command_buffer, swapchain.image_views().at(swapchain_image_index),
                            swapchain.extent());

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer fractal");
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
    cubey::input::PanZoom2DController view_controller_{cubey::input::PanZoom2DConfig{
        .center_x = -0.5F,
        .center_y = 0.0F,
        .scale = 1.35F,
    }};

    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
};

} // namespace

int run_fractal(const RunConfig& config) {
    FractalApp app(config);
    return app.run();
}

} // namespace cubey::examples::fractal
