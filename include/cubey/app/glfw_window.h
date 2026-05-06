#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace cubey::app {

struct GlfwWindowConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::string title = "cubey";
};

class GlfwWindow {
  public:
    explicit GlfwWindow(GlfwWindowConfig config);
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

    [[nodiscard]] bool framebuffer_resized() const {
        return framebuffer_resized_;
    }
    [[nodiscard]] bool consume_framebuffer_resized();
    [[nodiscard]] GLFWwindow* native_handle() const {
        return window_;
    }

  private:
    void create(GlfwWindowConfig config);
    void destroy();

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);

    bool glfw_initialized_ = false;
    bool framebuffer_resized_ = false;
    GLFWwindow* window_ = nullptr;
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
