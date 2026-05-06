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

Key to_key(int key) {
    switch (key) {
    case GLFW_KEY_ESCAPE:
        return Key::Escape;
    case GLFW_KEY_R:
        return Key::R;
    case GLFW_KEY_SPACE:
        return Key::Space;
    default:
        return Key::Unknown;
    }
}

KeyAction to_key_action(int action) {
    switch (action) {
    case GLFW_PRESS:
        return KeyAction::Press;
    case GLFW_RELEASE:
        return KeyAction::Release;
    case GLFW_REPEAT:
        return KeyAction::Repeat;
    default:
        return KeyAction::Unknown;
    }
}

MouseButton to_mouse_button(int button) {
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        return MouseButton::Left;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case GLFW_MOUSE_BUTTON_RIGHT:
        return MouseButton::Right;
    default:
        return MouseButton::Unknown;
    }
}

MouseButtonAction to_mouse_button_action(int action) {
    switch (action) {
    case GLFW_PRESS:
        return MouseButtonAction::Press;
    case GLFW_RELEASE:
        return MouseButtonAction::Release;
    default:
        return MouseButtonAction::Unknown;
    }
}

} // namespace

GlfwWindow::GlfwWindow(const GlfwWindowConfig& config) {
    create(config);
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
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW event polling requires a window");
    }
    glfwPollEvents();
}

void GlfwWindow::wait_events() const {
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW event waiting requires a window");
    }
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

CursorPosition GlfwWindow::cursor_position() const {
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW cursor position requires a window");
    }
    CursorPosition position{};
    glfwGetCursorPos(window_, &position.x, &position.y);
    return position;
}

void GlfwWindow::set_key_callback(std::function<void(const KeyEvent&)> callback) {
    key_callback_ = std::move(callback);
}

void GlfwWindow::set_mouse_button_callback(std::function<void(const MouseButtonEvent&)> callback) {
    mouse_button_callback_ = std::move(callback);
}

void GlfwWindow::set_cursor_position_callback(
    std::function<void(const CursorPositionEvent&)> callback) {
    cursor_position_callback_ = std::move(callback);
}

void GlfwWindow::set_scroll_callback(std::function<void(const ScrollEvent&)> callback) {
    scroll_callback_ = std::move(callback);
}

bool GlfwWindow::consume_framebuffer_resized() {
    const bool result = framebuffer_resized_;
    framebuffer_resized_ = false;
    return result;
}

void GlfwWindow::create(const GlfwWindowConfig& config) {
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
    glfwSetKeyCallback(window_, key_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetScrollCallback(window_, scroll_callback);
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

// GLFW fixes this callback signature.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void GlfwWindow::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto* app_window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (app_window != nullptr) {
        app_window->framebuffer_resized_ = true;
    }
}

void GlfwWindow::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* app_window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (app_window != nullptr && app_window->key_callback_) {
        app_window->key_callback_({
            .key = to_key(key),
            .action = to_key_action(action),
            .native_key = key,
            .native_scancode = scancode,
            .native_mods = mods,
        });
    }
}

void GlfwWindow::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    auto* app_window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (app_window != nullptr && app_window->mouse_button_callback_) {
        app_window->mouse_button_callback_({
            .button = to_mouse_button(button),
            .action = to_mouse_button_action(action),
            .cursor = app_window->cursor_position(),
            .native_button = button,
            .native_mods = mods,
        });
    }
}

void GlfwWindow::cursor_position_callback(GLFWwindow* window, double x, double y) {
    auto* app_window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (app_window != nullptr && app_window->cursor_position_callback_) {
        app_window->cursor_position_callback_({
            .cursor = {.x = x, .y = y},
        });
    }
}

void GlfwWindow::scroll_callback(GLFWwindow* window, double x_offset, double y_offset) {
    auto* app_window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    if (app_window != nullptr && app_window->scroll_callback_) {
        app_window->scroll_callback_({
            .x_offset = x_offset,
            .y_offset = y_offset,
            .cursor = app_window->cursor_position(),
        });
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
