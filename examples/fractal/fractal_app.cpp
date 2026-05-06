#include "fractal_app.h"

#include "fractal_view.h"

#include <cubey/app/glfw_window.h>
#include <cubey/app/windowed_host.h>
#include <cubey/image_io.h>
#include <cubey/spirv_io.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_FRACTAL_SHADER_DIR
#error "CUBEY_FRACTAL_SHADER_DIR must be defined by the fractal CMake target"
#endif

namespace cubey::examples::fractal {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr VkFormat kHeadlessOutputFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::size_t kOutputBytesPerPixel = 4;

[[nodiscard]] std::size_t checked_pixel_byte_size(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("fractal render dimensions must be positive");
    }

    const std::size_t checked_width = static_cast<std::size_t>(width);
    const std::size_t checked_height = static_cast<std::size_t>(height);
    if (checked_width > std::numeric_limits<std::size_t>::max() / checked_height) {
        throw std::runtime_error("fractal output is too large");
    }

    const std::size_t pixel_count = checked_width * checked_height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / kOutputBytesPerPixel) {
        throw std::runtime_error("fractal output is too large");
    }
    return pixel_count * kOutputBytesPerPixel;
}

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FRACTAL_SHADER_DIR) / filename;
}

class FractalApp {
  public:
    explicit FractalApp(RunConfig config) : config_(std::move(config)) {}

    FractalApp(const FractalApp&) = delete;
    FractalApp& operator=(const FractalApp&) = delete;

    ~FractalApp() {
        if (device_owner_.has_value()) {
            static_cast<void>(vkDeviceWaitIdle(device_owner_->handle()));
        }

        pipeline_.reset();
        pipeline_layout_.reset();
        device_owner_.reset();
        device_ = VK_NULL_HANDLE;
        instance_owner_.reset();
        instance_ = VK_NULL_HANDLE;
    }

    int run() {
        if (config_.headless) {
            create_headless_instance();
            create_headless_device();
            render_headless();
            return 0;
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
                    [this](cubey::app::WindowedAppContext& context) {
                        setup_input(context);
                        std::printf("fractal: %s rendering fullscreen fractal at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update = {},
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
    void create_headless_instance() {
        cubey::vulkan::InstanceConfig instance_config;
        instance_config.application_name = config_.title;
        instance_config.validation = config_.validation;
        instance_config.require_validation = config_.require_validation;

        instance_owner_.emplace(instance_config);
        instance_ = instance_owner_->handle();
    }

    void create_headless_device() {
        cubey::vulkan::DeviceConfig device_config;
        device_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        device_config.require_present = false;
        device_config.require_dynamic_rendering = true;

        if (!instance_owner_.has_value()) {
            throw std::runtime_error("Vulkan instance must exist before creating a device");
        }
        device_owner_.emplace(instance_owner_.value(), device_config);
        device_ = device_owner_->handle();
    }

    void setup_input(cubey::app::WindowedAppContext& context) {
        cubey::app::GlfwWindow* window = &context.window();
        window->set_cursor_position_callback(
            [this, window](const cubey::app::CursorPositionEvent& event) {
                if (!dragging_) {
                    return;
                }

                const VkExtent2D extent = window->framebuffer_extent();
                view_.pan_by_screen_delta(static_cast<float>(event.cursor.x - last_cursor_x_),
                                          static_cast<float>(event.cursor.y - last_cursor_y_),
                                          static_cast<float>(extent.width),
                                          static_cast<float>(extent.height));
                last_cursor_x_ = event.cursor.x;
                last_cursor_y_ = event.cursor.y;
            });
        window->set_key_callback([this, window](const cubey::app::KeyEvent& event) {
            if (event.action != cubey::app::KeyAction::Press) {
                return;
            }
            switch (event.key) {
            case cubey::app::Key::Escape:
                window->request_close();
                break;
            case cubey::app::Key::R:
                view_.reset();
                break;
            default:
                break;
            }
        });
        window->set_mouse_button_callback([this](const cubey::app::MouseButtonEvent& event) {
            if (event.button != cubey::app::MouseButton::Left) {
                return;
            }
            if (event.action == cubey::app::MouseButtonAction::Press) {
                dragging_ = true;
                last_cursor_x_ = event.cursor.x;
                last_cursor_y_ = event.cursor.y;
            } else if (event.action == cubey::app::MouseButtonAction::Release) {
                dragging_ = false;
            }
        });
        window->set_scroll_callback([this, window](const cubey::app::ScrollEvent& event) {
            const VkExtent2D extent = window->framebuffer_extent();
            const float factor = std::pow(0.86F, static_cast<float>(event.y_offset));
            view_.zoom_at(factor, static_cast<float>(event.cursor.x),
                          static_cast<float>(event.cursor.y), static_cast<float>(extent.width),
                          static_cast<float>(extent.height));
        });
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

    void record_headless_frame(cubey::vulkan::Image& render_target, VkExtent2D extent) {
        cubey::vulkan::ImmediateCommands commands(vulkan_device());
        const VkCommandBuffer command_buffer = commands.command_buffer();

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_color_attachment_transition(render_target.handle()));
        record_fractal_draw(command_buffer, render_target.view(), extent);
        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_readback_transition(render_target.handle()));
        commands.submit_and_wait();
    }

    void render_headless() {
        const VkExtent2D extent{config_.width, config_.height};
        cubey::vulkan::Image render_target(
            vulkan_device(),
            cubey::vulkan::color_render_target_image_config(extent, kHeadlessOutputFormat));
        create_pipeline(vulkan_device(), render_target.format(), extent);
        record_headless_frame(render_target, extent);

        const std::size_t byte_size = checked_pixel_byte_size(extent.width, extent.height);
        const VkDeviceSize readback_byte_size = static_cast<VkDeviceSize>(byte_size);
        cubey::vulkan::Buffer readback(vulkan_device(),
                                       cubey::vulkan::readback_buffer_config(readback_byte_size));
        cubey::vulkan::copy_image_to_buffer(vulkan_device(), render_target.handle(),
                                            readback.handle(), {extent.width, extent.height, 1});

        std::vector<std::uint8_t> pixels(byte_size);
        readback.download(pixels.data(), readback_byte_size);
        cubey::write_png_rgba8(config_.output_path, extent.width, extent.height, pixels);

        const std::string output_path = config_.output_path.string();
        std::printf("fractal: %s wrote %s at %ux%u\n", vulkan_device().device_name(),
                    output_path.c_str(), extent.width, extent.height);
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle after fractal headless");
    }

    cubey::vulkan::Device& vulkan_device() {
        if (!device_owner_.has_value()) {
            throw std::runtime_error("Vulkan device is not initialized");
        }
        return device_owner_.value();
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
    bool dragging_ = false;
    double last_cursor_x_ = 0.0;
    double last_cursor_y_ = 0.0;
    FractalView view_;

    std::optional<cubey::vulkan::Instance> instance_owner_;
    std::optional<cubey::vulkan::Device> device_owner_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
};

} // namespace

int run_fractal(const RunConfig& config) {
    FractalApp app(config);
    return app.run();
}

} // namespace cubey::examples::fractal
