#include "triangle_app.h"

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/swapchain.h>
#include <cubey/vulkan/visible_frame.h>
#include <cubey/vulkan/vk_check.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_TRIANGLE_SHADER_DIR
#error "CUBEY_TRIANGLE_SHADER_DIR must be defined by the triangle CMake target"
#endif

namespace cubey::examples::triangle {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

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
    return std::filesystem::path(CUBEY_TRIANGLE_SHADER_DIR) / filename;
}

class TriangleApp {
  public:
    explicit TriangleApp(RunConfig config) : config_(std::move(config)) {}

    TriangleApp(const TriangleApp&) = delete;
    TriangleApp& operator=(const TriangleApp&) = delete;

    ~TriangleApp() {
        if (device_ != VK_NULL_HANDLE) {
            static_cast<void>(vkDeviceWaitIdle(device_));
        }

        frame_resources_.reset();
        destroy_pipeline();
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
            throw std::runtime_error("triangle does not support --headless yet");
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
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void framebuffer_size_callback(GLFWwindow* window, int unused_width, int unused_height) {
        (void)unused_width;
        (void)unused_height;
        auto* app = static_cast<TriangleApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->framebuffer_resized_ = true;
        }
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
        create_pipeline();
    }

    void destroy_swapchain_resources() {
        destroy_pipeline();
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

    void create_pipeline() {
        const std::vector<std::uint32_t> vertex_code =
            read_spirv_file(shader_path("triangle.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            read_spirv_file(shader_path("triangle.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(vulkan_device(), vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(vulkan_device(), fragment_code);

        auto vertex_stage = vk_struct<VkPipelineShaderStageCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertex_stage.module = vertex_shader.handle();
        vertex_stage.pName = "main";

        auto fragment_stage = vk_struct<VkPipelineShaderStageCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragment_stage.module = fragment_shader.handle();
        fragment_stage.pName = "main";

        const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
            vertex_stage,
            fragment_stage,
        };

        auto vertex_input = vk_struct<VkPipelineVertexInputStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);

        auto input_assembly = vk_struct<VkPipelineInputAssemblyStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(swapchain().extent().width);
        viewport.height = static_cast<float>(swapchain().extent().height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchain().extent();

        auto viewport_state = vk_struct<VkPipelineViewportStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        auto rasterizer = vk_struct<VkPipelineRasterizationStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0F;

        auto multisample = vk_struct<VkPipelineMultisampleStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        auto color_blend = vk_struct<VkPipelineColorBlendStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        color_blend.attachmentCount = 1;
        color_blend.pAttachments = &color_blend_attachment;

        auto layout_info =
            vk_struct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        check(vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_),
              "vkCreatePipelineLayout");

        const VkFormat color_format = swapchain().format();
        auto rendering_info = vk_struct<VkPipelineRenderingCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachmentFormats = &color_format;

        auto pipeline_info = vk_struct<VkGraphicsPipelineCreateInfo>(
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        pipeline_info.pNext = &rendering_info;
        pipeline_info.stageCount = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_info.pStages = shader_stages.data();
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.layout = pipeline_layout_;

        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                        &pipeline_),
              "vkCreateGraphicsPipelines");
    }

    void destroy_pipeline() {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
    }

    void create_frame_resources() {
        frame_resources_.emplace(vulkan_device(), swapchain().image_count());
    }

    void transition_swapchain_image(VkCommandBuffer command_buffer, std::uint32_t image_index,
                                    VkImageLayout old_layout, VkImageLayout new_layout) const {
        auto barrier = vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchain().images().at(static_cast<std::size_t>(image_index));
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
            new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
                   new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        } else {
            throw std::runtime_error("unsupported swapchain image layout transition");
        }

        vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
    }

    static void record_triangle_frame_callback(void* user_data,
                                               const cubey::vulkan::VisibleFrameContext& context) {
        auto* app = static_cast<TriangleApp*>(user_data);
        if (app == nullptr) {
            throw std::runtime_error("triangle recorder requires app user data");
        }
        app->record_triangle_frame(context.command_buffer, context.image_index);
    }

    void record_triangle_frame(VkCommandBuffer command_buffer, std::uint32_t image_index) {
        auto begin =
            vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command_buffer, &begin), "vkBeginCommandBuffer triangle");

        transition_swapchain_image(command_buffer, image_index, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkClearValue clear{};
        clear.color = {{0.015F, 0.017F, 0.024F, 1.0F}};
        auto color_attachment =
            vk_struct<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        color_attachment.imageView =
            swapchain().image_views().at(static_cast<std::size_t>(image_index));
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = clear;

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = swapchain().extent();
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);
        vkCmdEndRendering(command_buffer);

        transition_swapchain_image(command_buffer, image_index,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer triangle");
    }

    cubey::vulkan::VisibleFrameResult draw_frame() {
        return cubey::vulkan::draw_visible_frame({
            .device = &vulkan_device(),
            .swapchain = &swapchain(),
            .frame_resources = &frame_resources(),
            .recorder = record_triangle_frame_callback,
            .user_data = this,
        });
    }

    void render_window() {
        std::printf("triangle: %s rendering dynamic triangle at %ux%u\n",
                    vulkan_device().device_name(), swapchain().extent().width,
                    swapchain().extent().height);

        std::uint32_t frame = 0;
        std::uint32_t consecutive_recreates = 0;
        while (glfwWindowShouldClose(window_) == 0 &&
               (config_.frames == 0 || frame < config_.frames)) {
            glfwPollEvents();

            if (framebuffer_resized_) {
                std::puts("framebuffer resized; recreating swapchain");
                recreate_swapchain_resources();
                consecutive_recreates = 0;
                continue;
            }

            cubey::vulkan::VisibleFrameResult result = draw_frame();
            if (result == cubey::vulkan::VisibleFrameResult::RecreateSwapchain) {
                ++consecutive_recreates;
                if (consecutive_recreates > 8) {
                    throw std::runtime_error(
                        "swapchain stayed out of date after 8 recreation attempts");
                }
                std::puts("swapchain out of date; recreating");
                recreate_swapchain_resources();
                continue;
            }

            consecutive_recreates = 0;
            ++frame;
        }

        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle after triangle");
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

    [[nodiscard]] const cubey::vulkan::Swapchain& swapchain() const {
        if (!swapchain_.has_value()) {
            throw std::runtime_error("swapchain is not initialized");
        }
        return swapchain_.value();
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
    GLFWwindow* window_ = nullptr;

    std::optional<cubey::vulkan::Instance> instance_owner_;
    std::optional<cubey::vulkan::Device> device_owner_;
    std::optional<cubey::vulkan::Swapchain> swapchain_;
    std::optional<cubey::vulkan::FrameResources> frame_resources_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace

int run_triangle(const RunConfig& config) {
    TriangleApp app(config);
    return app.run();
}

} // namespace cubey::examples::triangle
