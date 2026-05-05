#include "fractal_app.h"

#include "fractal_view.h"

#include <cubey/image_output.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/render_context.h>
#include <cubey/vulkan/rendering.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/swapchain.h>
#include <cubey/vulkan/vk_check.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::vector<std::uint32_t> read_spirv_file(const std::filesystem::path& path) {
    const std::uintmax_t byte_size = std::filesystem::file_size(path);
    if (byte_size == 0 || byte_size % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("invalid SPIR-V size for " + path.string());
    }
    if (byte_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        byte_size / sizeof(std::uint32_t) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("SPIR-V file is too large: " + path.string());
    }

    std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_size / sizeof(std::uint32_t)));
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open SPIR-V file: " + path.string());
    }

    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byte_size));
    if (!file) {
        throw std::runtime_error("failed to read SPIR-V file: " + path.string());
    }
    return code;
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
        if (device_ != VK_NULL_HANDLE) {
            static_cast<void>(vkDeviceWaitIdle(device_));
        }

        frame_resources_.reset();
        pipeline_.reset();
        pipeline_layout_.reset();
        swapchain_.reset();

        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        device_owner_.reset();
        device_ = VK_NULL_HANDLE;
        instance_owner_.reset();
        instance_ = VK_NULL_HANDLE;
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        if (glfw_initialized_) {
            glfwTerminate();
        }
    }

    int run() {
        if (config_.headless) {
            create_headless_instance();
            create_headless_device();
            render_headless();
            return 0;
        }

        init_window();
        create_instance();
        create_surface();
        create_device();
        create_swapchain_resources();
        create_frame_resources();
        render_window();
        return 0;
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

    void init_window() {
        if (glfwInit() == 0) {
            throw std::runtime_error("glfwInit failed");
        }
        glfw_initialized_ = true;

        if (glfwVulkanSupported() == 0) {
            throw std::runtime_error("GLFW reports Vulkan is not supported");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ =
            glfwCreateWindow(static_cast<int>(config_.width), static_cast<int>(config_.height),
                             config_.title.c_str(), nullptr, nullptr);
        if (window_ == nullptr) {
            throw std::runtime_error("glfwCreateWindow failed");
        }

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
        glfwSetCursorPosCallback(window_, cursor_position_callback);
        glfwSetKeyCallback(window_, key_callback);
        glfwSetMouseButtonCallback(window_, mouse_button_callback);
        glfwSetScrollCallback(window_, scroll_callback);
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void framebuffer_size_callback(GLFWwindow* window, int unused_width, int unused_height) {
        (void)unused_width;
        (void)unused_height;
        auto* app = static_cast<FractalApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->framebuffer_resized_ = true;
        }
    }

    static void cursor_position_callback(GLFWwindow* window, double x_position, double y_position) {
        auto* app = static_cast<FractalApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->handle_cursor_position(x_position, y_position);
        }
    }

    static void key_callback(GLFWwindow* window, int key, int unused_scancode, int action,
                             int unused_mods) {
        (void)unused_scancode;
        (void)unused_mods;
        auto* app = static_cast<FractalApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->handle_key(key, action);
        }
    }

    static void mouse_button_callback(GLFWwindow* window, int button, int action, int unused_mods) {
        (void)unused_mods;
        auto* app = static_cast<FractalApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->handle_mouse_button(button, action);
        }
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void scroll_callback(GLFWwindow* window, double unused_x_offset, double y_offset) {
        (void)unused_x_offset;
        auto* app = static_cast<FractalApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->handle_scroll(y_offset);
        }
    }

    void handle_cursor_position(double x_position, double y_position) {
        if (!dragging_) {
            return;
        }

        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        view_.pan_by_screen_delta(static_cast<float>(x_position - last_cursor_x_),
                                  static_cast<float>(y_position - last_cursor_y_),
                                  static_cast<float>(fb_width), static_cast<float>(fb_height));
        last_cursor_x_ = x_position;
        last_cursor_y_ = y_position;
    }

    void handle_key(int key, int action) {
        if (action != GLFW_PRESS) {
            return;
        }
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        } else if (key == GLFW_KEY_R) {
            view_.reset();
        }
    }

    void handle_mouse_button(int button, int action) {
        if (button != GLFW_MOUSE_BUTTON_LEFT) {
            return;
        }
        if (action == GLFW_PRESS) {
            dragging_ = true;
            glfwGetCursorPos(window_, &last_cursor_x_, &last_cursor_y_);
        } else if (action == GLFW_RELEASE) {
            dragging_ = false;
        }
    }

    void handle_scroll(double y_offset) {
        double cursor_x = 0.0;
        double cursor_y = 0.0;
        glfwGetCursorPos(window_, &cursor_x, &cursor_y);

        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        const float factor = std::pow(0.86F, static_cast<float>(y_offset));
        view_.zoom_at(factor, static_cast<float>(cursor_x), static_cast<float>(cursor_y),
                      static_cast<float>(fb_width), static_cast<float>(fb_height));
    }

    void create_instance() {
        std::uint32_t extension_count = 0;
        const char** required_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        if (required_extensions == nullptr || extension_count == 0) {
            throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
        }

        cubey::vulkan::InstanceConfig instance_config;
        instance_config.application_name = config_.title;
        instance_config.required_extensions.assign(required_extensions,
                                                   required_extensions + extension_count);
        instance_config.validation = config_.validation;
        instance_config.require_validation = config_.require_validation;

        instance_owner_.emplace(instance_config);
        instance_ = instance_owner_->handle();
    }

    void create_surface() {
        check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
              "glfwCreateWindowSurface");
    }

    void create_device() {
        cubey::vulkan::DeviceConfig device_config;
        device_config.surface = surface_;
        device_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        device_config.require_present = true;
        device_config.require_dynamic_rendering = true;

        if (!instance_owner_.has_value()) {
            throw std::runtime_error("Vulkan instance must exist before creating a device");
        }
        device_owner_.emplace(instance_owner_.value(), device_config);
        device_ = device_owner_->handle();
    }

    void wait_for_presentable_window_size() const {
        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        while ((fb_width == 0 || fb_height == 0) && glfwWindowShouldClose(window_) == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        }
        if (fb_width == 0 || fb_height == 0) {
            throw std::runtime_error(
                "window closed before a presentable framebuffer size was available");
        }
    }

    [[nodiscard]] VkExtent2D current_framebuffer_extent() const {
        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        return {
            static_cast<std::uint32_t>(fb_width),
            static_cast<std::uint32_t>(fb_height),
        };
    }

    void create_swapchain_resources() {
        create_swapchain();
        create_pipeline(swapchain().format(), swapchain().extent());
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_layout_.reset();
        swapchain_.reset();
    }

    void recreate_swapchain_resources() {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle before swapchain recreate");
        frame_resources_.reset();
        destroy_swapchain_resources();
        create_swapchain_resources();
        create_frame_resources();
    }

    void create_swapchain() {
        wait_for_presentable_window_size();

        cubey::vulkan::SwapchainConfig swapchain_config;
        swapchain_config.surface = surface_;
        swapchain_config.desired_extent = current_framebuffer_extent();
        swapchain_config.image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_.emplace(vulkan_device(), swapchain_config);
        framebuffer_resized_ = false;
    }

    void create_pipeline(VkFormat color_format, VkExtent2D extent) {
        const std::vector<std::uint32_t> vertex_code =
            read_spirv_file(shader_path("fractal.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            read_spirv_file(shader_path("fractal.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(vulkan_device(), vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(vulkan_device(), fragment_code);

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
        pipeline_layout_.emplace(vulkan_device(), layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = pipeline_layout().handle();
        pipeline_config.extent = extent;
        pipeline_config.color_format = color_format;
        pipeline_config.shader_stages = shader_stages;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        pipeline_.emplace(vulkan_device(), pipeline_info.create_info());
    }

    void create_frame_resources() {
        frame_resources_.emplace(vulkan_device(), swapchain().image_count());
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

    void record_fractal_frame(VkCommandBuffer command_buffer, std::uint32_t image_index) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain().images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        record_fractal_draw(command_buffer, swapchain().image_views().at(swapchain_image_index),
                            swapchain().extent());

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

    cubey::vulkan::RenderFrameResult draw_frame() {
        cubey::vulkan::RenderContext render_context({
            .device = &vulkan_device(),
            .swapchain = &swapchain(),
            .frame_resources = &frame_resources(),
        });

        cubey::vulkan::RenderFrame frame;
        cubey::vulkan::RenderFrameResult result = render_context.begin_frame(&frame);
        if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
            return result;
        }

        record_fractal_frame(frame.command_buffer, frame.image_index);
        return render_context.end_frame(frame);
    }

    void render_window() {
        std::printf("fractal: %s rendering fullscreen fractal at %ux%u\n",
                    vulkan_device().device_name(), swapchain().extent().width,
                    swapchain().extent().height);

        std::uint32_t frame = 0;
        cubey::vulkan::SwapchainRecreateTracker recreate_tracker;
        while (glfwWindowShouldClose(window_) == 0 &&
               (config_.frames == 0 || frame < config_.frames)) {
            glfwPollEvents();

            if (framebuffer_resized_) {
                std::puts("framebuffer resized; recreating swapchain");
                recreate_swapchain_resources();
                recreate_tracker.reset();
                continue;
            }

            cubey::vulkan::RenderFrameResult result = draw_frame();
            if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
                recreate_tracker.record_recreate_request();
                std::puts("swapchain out of date; recreating");
                recreate_swapchain_resources();
                continue;
            }

            recreate_tracker.reset();
            ++frame;
        }

        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle after fractal");
    }

    void render_headless() {
        const VkExtent2D extent{config_.width, config_.height};
        cubey::vulkan::Image render_target(
            vulkan_device(),
            cubey::vulkan::color_render_target_image_config(extent, kHeadlessOutputFormat));
        create_pipeline(render_target.format(), extent);
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

    cubey::vulkan::Swapchain& swapchain() {
        if (!swapchain_.has_value()) {
            throw std::runtime_error("swapchain is not initialized");
        }
        return swapchain_.value();
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

    cubey::vulkan::FrameResources& frame_resources() {
        if (!frame_resources_.has_value()) {
            throw std::runtime_error("frame resources are not initialized");
        }
        return frame_resources_.value();
    }

    RunConfig config_;
    bool glfw_initialized_ = false;
    bool framebuffer_resized_ = false;
    bool dragging_ = false;
    double last_cursor_x_ = 0.0;
    double last_cursor_y_ = 0.0;
    GLFWwindow* window_ = nullptr;
    FractalView view_;

    std::optional<cubey::vulkan::Instance> instance_owner_;
    std::optional<cubey::vulkan::Device> device_owner_;
    std::optional<cubey::vulkan::Swapchain> swapchain_;
    std::optional<cubey::vulkan::FrameResources> frame_resources_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
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
