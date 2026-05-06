#include <cubey/app/glfw_window.h>

#include <cubey/vulkan/vk_check.h>

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <utility>

namespace cubey::app {
namespace {

void validate_config(const GlfwWindowConfig& config) {
    if (config.width == 0 || config.height == 0) {
        throw std::runtime_error("GLFW window dimensions must be positive");
    }
}

} // namespace

GlfwWindow::GlfwWindow(GlfwWindowConfig config) {
    create(std::move(config));
}

GlfwWindow::~GlfwWindow() {
    destroy();
}

std::vector<const char*> GlfwWindow::required_instance_extensions() const {
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW instance extensions require a window");
    }

    std::uint32_t extension_count = 0;
    const char** required_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    if (required_extensions == nullptr || extension_count == 0) {
        throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
    }
    return {required_extensions, required_extensions + extension_count};
}

VkExtent2D GlfwWindow::framebuffer_extent() const {
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW framebuffer extent requires a window");
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
    };
}

void GlfwWindow::wait_for_presentable_framebuffer() const {
    VkExtent2D extent = framebuffer_extent();
    while ((extent.width == 0 || extent.height == 0) && !should_close()) {
        wait_events();
        extent = framebuffer_extent();
    }
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error(
            "window closed before a presentable framebuffer size was available");
    }
}

void GlfwWindow::poll_events() const {
    glfwPollEvents();
}

void GlfwWindow::wait_events() const {
    glfwWaitEvents();
}

bool GlfwWindow::should_close() const {
    if (window_ == nullptr) {
        return true;
    }
    return glfwWindowShouldClose(window_) != 0;
}

void GlfwWindow::request_close() const {
    if (window_ != nullptr) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void GlfwWindow::set_title(const char* title) const {
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW window title update requires a window");
    }
    glfwSetWindowTitle(window_, title);
}

bool GlfwWindow::consume_framebuffer_resized() {
    const bool result = framebuffer_resized_;
    framebuffer_resized_ = false;
    return result;
}

void GlfwWindow::create(GlfwWindowConfig config) {
    validate_config(config);

    if (glfwInit() == 0) {
        throw std::runtime_error("glfwInit failed");
    }
    glfw_initialized_ = true;

    if (glfwVulkanSupported() == 0) {
        throw std::runtime_error("GLFW reports Vulkan is not supported");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(static_cast<int>(config.width), static_cast<int>(config.height),
                               config.title.c_str(), nullptr, nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("glfwCreateWindow failed");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
}

void GlfwWindow::destroy() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfw_initialized_) {
        glfwTerminate();
        glfw_initialized_ = false;
    }
}

void GlfwWindow::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto* app_window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (app_window != nullptr) {
        app_window->framebuffer_resized_ = true;
    }
}

GlfwSurface::GlfwSurface(const GlfwWindow& window, VkInstance instance) : instance_(instance) {
    if (window.native_handle() == nullptr || instance_ == VK_NULL_HANDLE) {
        throw std::runtime_error("GLFW surface creation requires a window and Vulkan instance");
    }
    cubey::vulkan::check(
        glfwCreateWindowSurface(instance_, window.native_handle(), nullptr, &surface_),
        "glfwCreateWindowSurface");
}

GlfwSurface::~GlfwSurface() {
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
}

} // namespace cubey::app
