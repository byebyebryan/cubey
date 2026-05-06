#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace cubey::app {

enum class Key {
    Unknown,
    D,
    Escape,
    R,
    Space,
};

enum class KeyAction {
    Unknown,
    Press,
    Release,
    Repeat,
};

struct KeyEvent {
    Key key = Key::Unknown;
    KeyAction action = KeyAction::Unknown;
    int native_key = 0;
    int native_scancode = 0;
    int native_mods = 0;
};

enum class MouseButton {
    Unknown,
    Left,
    Middle,
    Right,
};

enum class MouseButtonAction {
    Unknown,
    Press,
    Release,
};

struct CursorPosition {
    double x = 0.0;
    double y = 0.0;
};

struct MouseButtonEvent {
    MouseButton button = MouseButton::Unknown;
    MouseButtonAction action = MouseButtonAction::Unknown;
    CursorPosition cursor{};
    int native_button = 0;
    int native_mods = 0;
};

struct CursorPositionEvent {
    CursorPosition cursor{};
};

struct ScrollEvent {
    double x_offset = 0.0;
    double y_offset = 0.0;
    CursorPosition cursor{};
};

struct GlfwWindowConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::string title = "cubey";
};

class GlfwWindow {
  public:
    explicit GlfwWindow(const GlfwWindowConfig& config);
    ~GlfwWindow();

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    [[nodiscard]] std::vector<const char*> required_instance_extensions() const;
    [[nodiscard]] VkExtent2D framebuffer_extent() const;
    void wait_for_presentable_framebuffer() const;

    void poll_events() const;
    void wait_events() const;
    [[nodiscard]] bool should_close() const;
    void request_close() const;
    void set_title(const char* title) const;
    [[nodiscard]] CursorPosition cursor_position() const;
    void set_key_callback(std::function<void(const KeyEvent&)> callback);
    void set_mouse_button_callback(std::function<void(const MouseButtonEvent&)> callback);
    void set_cursor_position_callback(std::function<void(const CursorPositionEvent&)> callback);
    void set_scroll_callback(std::function<void(const ScrollEvent&)> callback);

    [[nodiscard]] bool framebuffer_resized() const {
        return framebuffer_resized_;
    }
    [[nodiscard]] bool consume_framebuffer_resized();
    [[nodiscard]] GLFWwindow* native_handle() const {
        return window_;
    }

  private:
    void create(const GlfwWindowConfig& config);
    void destroy();

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    // GLFW fixes this callback signature.
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void cursor_position_callback(GLFWwindow* window, double x, double y);
    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void scroll_callback(GLFWwindow* window, double x_offset, double y_offset);

    bool glfw_initialized_ = false;
    bool framebuffer_resized_ = false;
    GLFWwindow* window_ = nullptr;
    std::function<void(const KeyEvent&)> key_callback_;
    std::function<void(const MouseButtonEvent&)> mouse_button_callback_;
    std::function<void(const CursorPositionEvent&)> cursor_position_callback_;
    std::function<void(const ScrollEvent&)> scroll_callback_;
};

class GlfwSurface {
  public:
    GlfwSurface(const GlfwWindow& window, VkInstance instance);
    ~GlfwSurface();

    GlfwSurface(const GlfwSurface&) = delete;
    GlfwSurface& operator=(const GlfwSurface&) = delete;

    [[nodiscard]] VkSurfaceKHR handle() const {
        return surface_;
    }

  private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
};

} // namespace cubey::app
