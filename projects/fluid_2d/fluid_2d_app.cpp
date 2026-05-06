#include "fluid_2d_app.h"

#include <cubey/app/glfw_window.h>
#include <cubey/app/windowed_host.h>
#include <cubey/frame_stats.h>
#include <cubey/spirv_io.h>
#include <cubey/vulkan/command_pool.h>
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
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_FLUID_2D_SHADER_DIR
#error "CUBEY_FLUID_2D_SHADER_DIR must be defined by the fluid_2d CMake target"
#endif

namespace cubey::projects::fluid_2d {
namespace {

using cubey::FrameStatsSample;
using cubey::FrameTiming;
using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

struct RenderPushConstants {
    std::array<float, 4> time_extent{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FLUID_2D_SHADER_DIR) / filename;
}

class Fluid2DApp {
  public:
    explicit Fluid2DApp(RunConfig config) : config_(std::move(config)) {}

    Fluid2DApp(const Fluid2DApp&) = delete;
    Fluid2DApp& operator=(const Fluid2DApp&) = delete;

    int run() {
        if (config_.headless) {
            throw std::runtime_error("fluid_2d does not support --headless yet");
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
                        setup_input(context);
                        std::printf("fluid_2d: %s rendering 2D fluid project at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update = {},
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                           std::uint32_t image_index, const FrameTiming& timing) {
                        record_frame(context, command_buffer, image_index, timing);
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
                        destroy_swapchain_resources();
                    },
            });
        return host.run();
    }

  private:
    static void setup_input(cubey::app::WindowedAppContext& context) {
        cubey::app::GlfwWindow* window = &context.window();
        window->set_key_callback([window](const cubey::app::KeyEvent& event) {
            if (event.action != cubey::app::KeyAction::Press) {
                return;
            }
            if (event.key == cubey::app::Key::Escape) {
                window->request_close();
            }
        });
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_layout_.reset();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("fluid_2d.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::read_spirv_file(shader_path("fluid_2d_render.frag.spv"));
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

        const VkPushConstantRange render_push_constant{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(RenderPushConstants),
        };
        const std::array<VkPushConstantRange, 1> push_constants{render_push_constant};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = {},
            .push_constants = push_constants,
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

    void record_fullscreen_draw(VkCommandBuffer command_buffer, VkImageView image_view,
                                VkExtent2D extent, const FrameTiming& timing) const {
        VkClearValue clear{};
        clear.color = {{0.006F, 0.008F, 0.014F, 1.0F}};
        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(image_view, clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;

        const RenderPushConstants push_constants{
            .time_extent =
                {
                    static_cast<float>(timing.elapsed_seconds),
                    static_cast<float>(extent.width),
                    static_cast<float>(extent.height),
                    0.0F,
                },
        };

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        vkCmdPushConstants(command_buffer, pipeline_layout().handle(), VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push_constants), &push_constants);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);
        vkCmdEndRendering(command_buffer);
    }

    void record_frame(cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                      std::uint32_t image_index, const FrameTiming& timing) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        cubey::vulkan::Swapchain& swapchain = context.swapchain();
        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain.images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        record_fullscreen_draw(command_buffer, swapchain.image_views().at(swapchain_image_index),
                               swapchain.extent(), timing);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer fluid_2d");
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
    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
};

} // namespace

int run_fluid_2d(const RunConfig& config) {
    Fluid2DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid_2d
