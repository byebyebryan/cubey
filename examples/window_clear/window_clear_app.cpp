#include "window_clear_app.h"

#include <cubey/vulkan/device.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/vk_check.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::examples::window_clear {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

class WindowClearApp {
  public:
    explicit WindowClearApp(RunConfig config) : config_(std::move(config)) {}

    WindowClearApp(const WindowClearApp&) = delete;
    WindowClearApp& operator=(const WindowClearApp&) = delete;

    ~WindowClearApp() {
        if (device_ != VK_NULL_HANDLE) {
            static_cast<void>(vkDeviceWaitIdle(device_));
        }

        destroy_sync();
        destroy_commands();
        destroy_framebuffers();
        destroy_render_pass();
        destroy_swapchain_views();
        destroy_swapchain();

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
            throw std::runtime_error("window_clear does not support --headless yet");
        }

        init_window();
        create_instance();
        create_surface();
        create_device();
        create_swapchain_resources();
        create_commands();
        create_sync();
        render_window();
        return 0;
    }

  private:
    enum class FrameResult : std::uint8_t {
        Rendered,
        RecreateSwapchain,
    };

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
        auto* app = static_cast<WindowClearApp*>(glfwGetWindowUserPointer(window));
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

        if (!instance_owner_.has_value()) {
            throw std::runtime_error("Vulkan instance must exist before creating a device");
        }
        device_owner_.emplace(instance_owner_.value(), device_config);
        physical_device_ = device_owner_->physical_device();
        device_properties_ = device_owner_->properties();
        device_ = device_owner_->handle();
        queue_ = device_owner_->queue();
        queue_family_ = device_owner_->queue_family();
    }

    static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR flags) {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> choices{
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };

        for (VkCompositeAlphaFlagBitsKHR choice : choices) {
            if ((flags & choice) != 0) {
                return choice;
            }
        }
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
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

    void create_swapchain_resources() {
        create_swapchain();
        create_swapchain_views();
        create_render_pass();
        create_framebuffers();
    }

    void destroy_swapchain_resources() {
        destroy_framebuffers();
        destroy_render_pass();
        destroy_swapchain_views();
        destroy_swapchain();
    }

    void recreate_swapchain_resources() {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle before swapchain recreate");
        destroy_swapchain_resources();
        create_swapchain_resources();
    }

    void create_swapchain() {
        wait_for_presentable_window_size();

        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            throw std::runtime_error("surface does not support color-attachment swapchain images");
        }

        std::uint32_t format_count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count,
                                                   nullptr),
              "vkGetPhysicalDeviceSurfaceFormatsKHR count");
        if (format_count == 0) {
            throw std::runtime_error("surface reported no formats");
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count,
                                                   formats.data()),
              "vkGetPhysicalDeviceSurfaceFormatsKHR");

        surface_format_ = formats[0];
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
                format.format == VK_FORMAT_R8G8B8A8_UNORM) {
                surface_format_ = format;
                break;
            }
        }

        if (caps.currentExtent.width != UINT32_MAX) {
            swapchain_extent_ = caps.currentExtent;
        } else {
            int fb_width = 0;
            int fb_height = 0;
            glfwGetFramebufferSize(window_, &fb_width, &fb_height);
            const auto clamp_dimension = [](int value, std::uint32_t minimum,
                                            std::uint32_t maximum) {
                return std::clamp(static_cast<std::uint32_t>(std::max(value, 1)), minimum, maximum);
            };
            swapchain_extent_.width =
                clamp_dimension(fb_width, caps.minImageExtent.width, caps.maxImageExtent.width);
            swapchain_extent_.height =
                clamp_dimension(fb_height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        std::uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) {
            image_count = std::min(image_count, caps.maxImageCount);
        }

        auto info =
            vk_struct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        info.surface = surface_;
        info.minImageCount = image_count;
        info.imageFormat = surface_format_.format;
        info.imageColorSpace = surface_format_.colorSpace;
        info.imageExtent = swapchain_extent_;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha);
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;

        check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");

        std::uint32_t actual_count = 0;
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, nullptr),
              "vkGetSwapchainImagesKHR count");
        swapchain_images_.resize(actual_count);
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, swapchain_images_.data()),
              "vkGetSwapchainImagesKHR");
        framebuffer_resized_ = false;
    }

    void destroy_swapchain() {
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        swapchain_images_.clear();
    }

    VkImageView create_image_view(VkImage image, VkFormat format) const {
        auto info = vk_struct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;

        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device_, &info, nullptr, &view), "vkCreateImageView");
        return view;
    }

    void create_swapchain_views() {
        swapchain_views_.reserve(swapchain_images_.size());
        for (VkImage image : swapchain_images_) {
            swapchain_views_.push_back(create_image_view(image, surface_format_.format));
        }
    }

    void destroy_swapchain_views() {
        for (VkImageView view : swapchain_views_) {
            vkDestroyImageView(device_, view, nullptr);
        }
        swapchain_views_.clear();
    }

    void create_render_pass() {
        VkAttachmentDescription color_attachment{};
        color_attachment.format = surface_format_.format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        auto info = vk_struct<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
        info.attachmentCount = 1;
        info.pAttachments = &color_attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        check(vkCreateRenderPass(device_, &info, nullptr, &render_pass_), "vkCreateRenderPass");
    }

    void destroy_render_pass() {
        if (render_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }
    }

    void create_framebuffers() {
        framebuffers_.reserve(swapchain_views_.size());
        for (VkImageView view : swapchain_views_) {
            auto info =
                vk_struct<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
            info.renderPass = render_pass_;
            info.attachmentCount = 1;
            info.pAttachments = &view;
            info.width = swapchain_extent_.width;
            info.height = swapchain_extent_.height;
            info.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            check(vkCreateFramebuffer(device_, &info, nullptr, &framebuffer),
                  "vkCreateFramebuffer");
            framebuffers_.push_back(framebuffer);
        }
    }

    void destroy_framebuffers() {
        for (VkFramebuffer framebuffer : framebuffers_) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();
    }

    void create_commands() {
        auto pool_info =
            vk_struct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_;
        check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
              "vkCreateCommandPool");

        auto alloc =
            vk_struct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &alloc, &command_buffer_),
              "vkAllocateCommandBuffers");
    }

    void destroy_commands() {
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
            command_buffer_ = VK_NULL_HANDLE;
        }
    }

    void create_sync() {
        auto semaphore_info =
            vk_struct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_),
              "vkCreateSemaphore image_available");
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &present_ready_),
              "vkCreateSemaphore present_ready");

        auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device_, &fence_info, nullptr, &frame_fence_), "vkCreateFence frame");
    }

    void destroy_sync() {
        if (frame_fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frame_fence_, nullptr);
            frame_fence_ = VK_NULL_HANDLE;
        }
        if (present_ready_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, present_ready_, nullptr);
            present_ready_ = VK_NULL_HANDLE;
        }
        if (image_available_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, image_available_, nullptr);
            image_available_ = VK_NULL_HANDLE;
        }
    }

    void record_clear_frame(std::uint32_t image_index) {
        auto begin =
            vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command_buffer_, &begin), "vkBeginCommandBuffer window_clear");

        VkClearValue clear{};
        clear.color = {{0.02f, 0.025f, 0.035f, 1.0f}};

        auto pass = vk_struct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
        pass.renderPass = render_pass_;
        pass.framebuffer = framebuffers_.at(static_cast<std::size_t>(image_index));
        pass.renderArea.offset = {0, 0};
        pass.renderArea.extent = swapchain_extent_;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;

        vkCmdBeginRenderPass(command_buffer_, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(command_buffer_);

        check(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer window_clear");
    }

    FrameResult draw_frame() {
        check(vkWaitForFences(device_, 1, &frame_fence_, VK_TRUE, UINT64_MAX),
              "vkWaitForFences frame");

        std::uint32_t image_index = 0;
        VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_,
                                                  VK_NULL_HANDLE, &image_index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            return FrameResult::RecreateSwapchain;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            check(acquired, "vkAcquireNextImageKHR");
        }
        bool recreate_after_present = acquired == VK_SUBOPTIMAL_KHR;

        check(vkResetFences(device_, 1, &frame_fence_), "vkResetFences frame");
        check(vkResetCommandBuffer(command_buffer_, 0), "vkResetCommandBuffer frame");
        record_clear_frame(image_index);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available_;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer_;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &present_ready_;
        check(vkQueueSubmit(queue_, 1, &submit, frame_fence_), "vkQueueSubmit window_clear");

        auto present = vk_struct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &present_ready_;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index;
        VkResult presented = vkQueuePresentKHR(queue_, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            recreate_after_present = true;
        } else if (presented != VK_SUCCESS) {
            check(presented, "vkQueuePresentKHR");
        }

        return recreate_after_present ? FrameResult::RecreateSwapchain : FrameResult::Rendered;
    }

    void render_window() {
        std::printf("window_clear: %s clearing swapchain at %ux%u\n", device_properties_.deviceName,
                    swapchain_extent_.width, swapchain_extent_.height);

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

            FrameResult result = draw_frame();
            if (result == FrameResult::RecreateSwapchain) {
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

        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle after window_clear");
    }

    RunConfig config_;
    bool glfw_initialized_ = false;
    bool framebuffer_resized_ = false;
    GLFWwindow* window_ = nullptr;

    std::optional<cubey::vulkan::Instance> instance_owner_;
    std::optional<cubey::vulkan::Device> device_owner_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties device_properties_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format_{};
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkSemaphore present_ready_ = VK_NULL_HANDLE;
    VkFence frame_fence_ = VK_NULL_HANDLE;
};

} // namespace

int run_window_clear(const RunConfig& config) {
    WindowClearApp app(config);
    return app.run();
}

} // namespace cubey::examples::window_clear
