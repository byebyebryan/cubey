#include "config.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct Options {
    bool headless = false;
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t frames = 0;
    std::string output = "cubey-vulkan.ppm";
};

struct PushConstants {
    uint32_t width;
    uint32_t height;
    float time;
};

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 2> uv;
    std::array<float, 3> color;
};

struct Uniforms {
    std::array<float, 16> mvp;
};

using Mat4 = std::array<float, 16>;

static float radians(float degrees) {
    constexpr float pi = 3.14159265358979323846f;
    return degrees * pi / 180.0f;
}

static Mat4 identity() {
    Mat4 m{};
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
    return m;
}

static Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (size_t col = 0; col < 4; ++col) {
        for (size_t row = 0; row < 4; ++row) {
            for (size_t k = 0; k < 4; ++k) {
                result[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }
    return result;
}

static Mat4 translation(float x, float y, float z) {
    Mat4 m = identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

static Mat4 rotation_x(float angle) {
    Mat4 m = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
    return m;
}

static Mat4 rotation_y(float angle) {
    Mat4 m = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
    return m;
}

static Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane) {
    float f = 1.0f / std::tan(fovy * 0.5f);
    Mat4 m{};
    m[0] = f / aspect;
    m[5] = -f;
    m[10] = far_plane / (near_plane - far_plane);
    m[11] = -1.0f;
    m[14] = (far_plane * near_plane) / (near_plane - far_plane);
    return m;
}

static const std::array<Vertex, 24>& cube_vertices() {
    static const std::array<Vertex, 24> vertices{{
        {{{-1.0f, -1.0f,  1.0f}}, {{0.0f, 1.0f}}, {{1.00f, 0.80f, 0.35f}}},
        {{{ 1.0f, -1.0f,  1.0f}}, {{1.0f, 1.0f}}, {{1.00f, 0.80f, 0.35f}}},
        {{{ 1.0f,  1.0f,  1.0f}}, {{1.0f, 0.0f}}, {{1.00f, 0.80f, 0.35f}}},
        {{{-1.0f,  1.0f,  1.0f}}, {{0.0f, 0.0f}}, {{1.00f, 0.80f, 0.35f}}},

        {{{ 1.0f, -1.0f, -1.0f}}, {{0.0f, 1.0f}}, {{0.30f, 0.85f, 1.00f}}},
        {{{-1.0f, -1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{0.30f, 0.85f, 1.00f}}},
        {{{-1.0f,  1.0f, -1.0f}}, {{1.0f, 0.0f}}, {{0.30f, 0.85f, 1.00f}}},
        {{{ 1.0f,  1.0f, -1.0f}}, {{0.0f, 0.0f}}, {{0.30f, 0.85f, 1.00f}}},

        {{{-1.0f, -1.0f, -1.0f}}, {{0.0f, 1.0f}}, {{0.95f, 0.45f, 0.65f}}},
        {{{-1.0f, -1.0f,  1.0f}}, {{1.0f, 1.0f}}, {{0.95f, 0.45f, 0.65f}}},
        {{{-1.0f,  1.0f,  1.0f}}, {{1.0f, 0.0f}}, {{0.95f, 0.45f, 0.65f}}},
        {{{-1.0f,  1.0f, -1.0f}}, {{0.0f, 0.0f}}, {{0.95f, 0.45f, 0.65f}}},

        {{{ 1.0f, -1.0f,  1.0f}}, {{0.0f, 1.0f}}, {{0.55f, 0.95f, 0.45f}}},
        {{{ 1.0f, -1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{0.55f, 0.95f, 0.45f}}},
        {{{ 1.0f,  1.0f, -1.0f}}, {{1.0f, 0.0f}}, {{0.55f, 0.95f, 0.45f}}},
        {{{ 1.0f,  1.0f,  1.0f}}, {{0.0f, 0.0f}}, {{0.55f, 0.95f, 0.45f}}},

        {{{-1.0f,  1.0f,  1.0f}}, {{0.0f, 1.0f}}, {{0.70f, 0.55f, 1.00f}}},
        {{{ 1.0f,  1.0f,  1.0f}}, {{1.0f, 1.0f}}, {{0.70f, 0.55f, 1.00f}}},
        {{{ 1.0f,  1.0f, -1.0f}}, {{1.0f, 0.0f}}, {{0.70f, 0.55f, 1.00f}}},
        {{{-1.0f,  1.0f, -1.0f}}, {{0.0f, 0.0f}}, {{0.70f, 0.55f, 1.00f}}},

        {{{-1.0f, -1.0f, -1.0f}}, {{0.0f, 1.0f}}, {{0.95f, 0.95f, 0.40f}}},
        {{{ 1.0f, -1.0f, -1.0f}}, {{1.0f, 1.0f}}, {{0.95f, 0.95f, 0.40f}}},
        {{{ 1.0f, -1.0f,  1.0f}}, {{1.0f, 0.0f}}, {{0.95f, 0.95f, 0.40f}}},
        {{{-1.0f, -1.0f,  1.0f}}, {{0.0f, 0.0f}}, {{0.95f, 0.95f, 0.40f}}},
    }};
    return vertices;
}

static const std::array<uint16_t, 36>& cube_indices() {
    static const std::array<uint16_t, 36> indices{{
         0,  1,  2,  2,  3,  0,
         4,  5,  6,  6,  7,  4,
         8,  9, 10, 10, 11,  8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20,
    }};
    return indices;
}

static void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed with VkResult " + std::to_string(result));
    }
}

template <typename T>
static T vk_struct(VkStructureType type) {
    T value{};
    value.sType = type;
    return value;
}

static std::vector<uint32_t> read_spirv(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::string("failed to open shader: ") + path);
    }

    std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        throw std::runtime_error("SPIR-V shader has invalid byte size");
    }

    std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) {
        throw std::runtime_error("failed to read SPIR-V shader");
    }
    return code;
}

static Options parse_args(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--headless") {
            opts.headless = true;
        } else if (arg == "--output") {
            opts.output = need_value("--output");
        } else if (arg == "--width") {
            opts.width = static_cast<uint32_t>(std::strtoul(need_value("--width"), nullptr, 10));
        } else if (arg == "--height") {
            opts.height = static_cast<uint32_t>(std::strtoul(need_value("--height"), nullptr, 10));
        } else if (arg == "--frames") {
            opts.frames = static_cast<uint32_t>(std::strtoul(need_value("--frames"), nullptr, 10));
        } else {
            throw std::runtime_error(std::string("unknown argument: ") + std::string(arg));
        }
    }
    if (opts.width == 0 || opts.height == 0) {
        throw std::runtime_error("width and height must be positive");
    }
    if (opts.headless && opts.frames == 0) {
        opts.frames = 1;
    }
    return opts;
}

class VulkanGraphicsSpike {
public:
    explicit VulkanGraphicsSpike(const Options& opts) : opts_(opts) {}

    ~VulkanGraphicsSpike() {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
        }

        destroy_framebuffers();
        destroy_render_pass();
        destroy_swapchain_views();
        destroy_swapchain();
        destroy_depth_resources();
        destroy_offscreen_resources();
        destroy_readback_resources();
        if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
        destroy_source_resources();
        destroy_uniform_resources();
        destroy_mesh_resources();
        destroy_descriptor_pool();
        destroy_graphics_pipeline();
        if (compute_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, compute_pipeline_, nullptr);
        if (compute_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, compute_layout_, nullptr);
        if (graphics_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, graphics_set_layout_, nullptr);
        if (compute_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, compute_set_layout_, nullptr);
        if (command_pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, command_pool_, nullptr);
        if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
        if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
        if (window_ != nullptr) glfwDestroyWindow(window_);
        if (glfw_initialized_) glfwTerminate();
    }

    void run() {
        if (!opts_.headless) {
            init_window();
        }
        create_instance();
        if (!opts_.headless) {
            create_surface();
        }
        select_physical_device();
        create_device();
        if (!opts_.headless) {
            create_swapchain();
            create_swapchain_views();
        }

        create_source_image();
        create_sampler();
        create_mesh_buffers();
        create_uniform_buffer();
        if (opts_.headless) {
            create_offscreen_target();
            create_readback_buffer();
        }
        create_depth_target();
        create_render_pass();
        create_framebuffers();
        create_descriptor_layouts();
        create_pipelines();
        create_descriptors();
        create_commands();

        if (opts_.headless) {
            render_headless();
            pixels_ = read_pixels();
            verify_pixels();
            write_ppm();
        } else {
            render_window();
        }
    }

    const char* device_name() const {
        return device_properties_.deviceName;
    }

private:
    enum class FrameResult {
        Rendered,
        RecreateSwapchain,
    };

    void init_window() {
        if (!glfwInit()) {
            throw std::runtime_error("glfwInit failed");
        }
        glfw_initialized_ = true;

        if (!glfwVulkanSupported()) {
            throw std::runtime_error("GLFW reports Vulkan is not supported");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(static_cast<int>(opts_.width), static_cast<int>(opts_.height), "cubey vulkan", nullptr, nullptr);
        if (window_ == nullptr) {
            throw std::runtime_error("glfwCreateWindow failed");
        }
    }

    void create_instance() {
        auto app = vk_struct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "cubey vulkan spike";
        app.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
        app.pEngineName = "cubey";
        app.engineVersion = VK_MAKE_VERSION(0, 2, 0);
        app.apiVersion = VK_API_VERSION_1_2;

        auto info = vk_struct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        info.pApplicationInfo = &app;
        std::vector<const char*> extensions;
        if (!opts_.headless) {
            uint32_t extension_count = 0;
            const char** required = glfwGetRequiredInstanceExtensions(&extension_count);
            if (required == nullptr || extension_count == 0) {
                throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
            }
            extensions.assign(required, required + extension_count);
            info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            info.ppEnabledExtensionNames = extensions.data();
        }
        check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
    }

    void create_surface() {
        check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "glfwCreateWindowSurface");
    }

    bool device_supports_swapchain(VkPhysicalDevice device) const {
        uint32_t count = 0;
        check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "vkEnumerateDeviceExtensionProperties count");
        std::vector<VkExtensionProperties> extensions(count);
        check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()), "vkEnumerateDeviceExtensionProperties");
        for (const VkExtensionProperties& extension : extensions) {
            if (std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
                return true;
            }
        }
        return false;
    }

    void select_physical_device() {
        uint32_t device_count = 0;
        check(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr), "vkEnumeratePhysicalDevices count");
        if (device_count == 0) {
            throw std::runtime_error("no Vulkan physical devices found");
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        check(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()), "vkEnumeratePhysicalDevices");

        for (VkPhysicalDevice candidate : devices) {
            if (!opts_.headless && !device_supports_swapchain(candidate)) {
                continue;
            }

            uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());

            for (uint32_t i = 0; i < family_count; ++i) {
                VkBool32 present_supported = VK_TRUE;
                if (!opts_.headless) {
                    check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present_supported), "vkGetPhysicalDeviceSurfaceSupportKHR");
                }

                VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
                if ((families[i].queueFlags & required) == required && present_supported == VK_TRUE) {
                    physical_device_ = candidate;
                    queue_family_ = i;
                    vkGetPhysicalDeviceProperties(physical_device_, &device_properties_);
                    return;
                }
            }
        }

        throw std::runtime_error(opts_.headless
            ? "no Vulkan device with graphics and compute queues found"
            : "no Vulkan device with one queue family supporting graphics, compute, and present found");
    }

    void create_device() {
        float priority = 1.0f;
        auto queue_info = vk_struct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queue_info.queueFamilyIndex = queue_family_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        auto info = vk_struct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue_info;
        std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (!opts_.headless) {
            info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            info.ppEnabledExtensionNames = extensions.data();
        }
        check(vkCreateDevice(physical_device_, &info, nullptr, &device_), "vkCreateDevice");
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    }

    static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR flags) {
        constexpr VkCompositeAlphaFlagBitsKHR choices[] = {
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

    void create_swapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            throw std::runtime_error("surface does not support color-attachment swapchain images");
        }

        uint32_t format_count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR count");
        if (format_count == 0) {
            throw std::runtime_error("surface reported no formats");
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");

        surface_format_ = formats[0];
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) {
                surface_format_ = format;
                break;
            }
        }

        if (caps.currentExtent.width != UINT32_MAX) {
            swapchain_extent_ = caps.currentExtent;
        } else {
            int fb_w = 0;
            int fb_h = 0;
            glfwGetFramebufferSize(window_, &fb_w, &fb_h);
            swapchain_extent_.width = std::clamp(static_cast<uint32_t>(std::max(fb_w, 1)), caps.minImageExtent.width, caps.maxImageExtent.width);
            swapchain_extent_.height = std::clamp(static_cast<uint32_t>(std::max(fb_h, 1)), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        opts_.width = swapchain_extent_.width;
        opts_.height = swapchain_extent_.height;

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) {
            image_count = std::min(image_count, caps.maxImageCount);
        }

        auto info = vk_struct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
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

        uint32_t actual_count = 0;
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, nullptr), "vkGetSwapchainImagesKHR count");
        swapchain_images_.resize(actual_count);
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, swapchain_images_.data()), "vkGetSwapchainImagesKHR");
    }

    void destroy_swapchain() {
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        swapchain_images_.clear();
    }

    void destroy_swapchain_views() {
        for (VkImageView view : swapchain_views_) {
            vkDestroyImageView(device_, view, nullptr);
        }
        swapchain_views_.clear();
    }

    void destroy_framebuffers() {
        for (VkFramebuffer framebuffer : framebuffers_) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();
    }

    void destroy_render_pass() {
        if (render_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }
    }

    void destroy_graphics_pipeline() {
        if (graphics_pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, graphics_pipeline_, nullptr);
            graphics_pipeline_ = VK_NULL_HANDLE;
        }
        if (graphics_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, graphics_layout_, nullptr);
            graphics_layout_ = VK_NULL_HANDLE;
        }
    }

    void destroy_descriptor_pool() {
        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        compute_set_ = VK_NULL_HANDLE;
        graphics_set_ = VK_NULL_HANDLE;
    }

    void destroy_source_resources() {
        if (source_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, source_view_, nullptr);
            source_view_ = VK_NULL_HANDLE;
        }
        if (source_image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, source_image_, nullptr);
            source_image_ = VK_NULL_HANDLE;
        }
        if (source_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, source_memory_, nullptr);
            source_memory_ = VK_NULL_HANDLE;
        }
        source_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void destroy_offscreen_resources() {
        if (offscreen_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, offscreen_view_, nullptr);
            offscreen_view_ = VK_NULL_HANDLE;
        }
        if (offscreen_image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, offscreen_image_, nullptr);
            offscreen_image_ = VK_NULL_HANDLE;
        }
        if (offscreen_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, offscreen_memory_, nullptr);
            offscreen_memory_ = VK_NULL_HANDLE;
        }
    }

    void destroy_readback_resources() {
        if (readback_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, readback_buffer_, nullptr);
            readback_buffer_ = VK_NULL_HANDLE;
        }
        if (readback_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, readback_memory_, nullptr);
            readback_memory_ = VK_NULL_HANDLE;
        }
        readback_size_ = 0;
    }

    void destroy_depth_resources() {
        if (depth_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, depth_view_, nullptr);
            depth_view_ = VK_NULL_HANDLE;
        }
        if (depth_image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, depth_image_, nullptr);
            depth_image_ = VK_NULL_HANDLE;
        }
        if (depth_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, depth_memory_, nullptr);
            depth_memory_ = VK_NULL_HANDLE;
        }
    }

    void destroy_mesh_resources() {
        if (index_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, index_buffer_, nullptr);
            index_buffer_ = VK_NULL_HANDLE;
        }
        if (index_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, index_memory_, nullptr);
            index_memory_ = VK_NULL_HANDLE;
        }
        if (vertex_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, vertex_buffer_, nullptr);
            vertex_buffer_ = VK_NULL_HANDLE;
        }
        if (vertex_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, vertex_memory_, nullptr);
            vertex_memory_ = VK_NULL_HANDLE;
        }
    }

    void destroy_uniform_resources() {
        if (uniform_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, uniform_buffer_, nullptr);
            uniform_buffer_ = VK_NULL_HANDLE;
        }
        if (uniform_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, uniform_memory_, nullptr);
            uniform_memory_ = VK_NULL_HANDLE;
        }
    }

    void recreate_window_resources() {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle before swapchain recreate");

        destroy_descriptor_pool();
        destroy_graphics_pipeline();
        destroy_framebuffers();
        destroy_render_pass();
        destroy_depth_resources();
        destroy_source_resources();
        destroy_swapchain_views();
        destroy_swapchain();

        create_swapchain();
        create_swapchain_views();
        create_source_image();
        create_depth_target();
        create_render_pass();
        create_framebuffers();
        create_graphics_pipeline();
        create_descriptors();
    }

    void require_format_features(VkFormat format, VkFormatFeatureFlags required, const char* label) const {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical_device_, format, &props);
        if ((props.optimalTilingFeatures & required) != required) {
            throw std::runtime_error(std::string("format lacks required optimal tiling features for ") + label);
        }
    }

    uint32_t find_memory_type(uint32_t bits, VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &props);
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            bool compatible = (bits & (1u << i)) != 0;
            bool has_flags = (props.memoryTypes[i].propertyFlags & required) == required;
            if (compatible && has_flags) {
                return i;
            }
        }
        throw std::runtime_error("no compatible Vulkan memory type found");
    }

    void create_buffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    ) {
        auto info = vk_struct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &info, nullptr, &buffer), "vkCreateBuffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, buffer, &req);

        auto alloc = vk_struct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, properties);
        check(vkAllocateMemory(device_, &alloc, nullptr, &memory), "vkAllocateMemory buffer");
        check(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory");
    }

    void upload_to_memory(VkDeviceMemory memory, const void* data, size_t size) {
        void* mapped = nullptr;
        check(vkMapMemory(device_, memory, 0, static_cast<VkDeviceSize>(size), 0, &mapped), "vkMapMemory upload");
        std::memcpy(mapped, data, size);
        vkUnmapMemory(device_, memory);
    }

    void create_image(
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkImage& image,
        VkDeviceMemory& memory
    ) {
        auto info = vk_struct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_, &info, nullptr, &image), "vkCreateImage");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, image, &req);

        auto alloc = vk_struct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, properties);
        check(vkAllocateMemory(device_, &alloc, nullptr, &memory), "vkAllocateMemory image");
        check(vkBindImageMemory(device_, image, memory, 0), "vkBindImageMemory");
    }

    VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
        auto info = vk_struct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange.aspectMask = aspect;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device_, &info, nullptr, &view), "vkCreateImageView");
        return view;
    }

    void create_source_image() {
        require_format_features(
            source_format_,
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
            "compute source image"
        );
        create_image(
            opts_.width,
            opts_.height,
            source_format_,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            source_image_,
            source_memory_
        );
        source_view_ = create_image_view(source_image_, source_format_);
    }

    void create_sampler() {
        auto info = vk_struct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 1.0f;
        check(vkCreateSampler(device_, &info, nullptr, &sampler_), "vkCreateSampler");
    }

    void create_mesh_buffers() {
        const auto& vertices = cube_vertices();
        VkDeviceSize vertex_size = static_cast<VkDeviceSize>(vertices.size() * sizeof(Vertex));
        create_buffer(
            vertex_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertex_buffer_,
            vertex_memory_
        );
        upload_to_memory(vertex_memory_, vertices.data(), static_cast<size_t>(vertex_size));

        const auto& indices = cube_indices();
        index_count_ = static_cast<uint32_t>(indices.size());
        VkDeviceSize index_size = static_cast<VkDeviceSize>(indices.size() * sizeof(uint16_t));
        create_buffer(
            index_size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            index_buffer_,
            index_memory_
        );
        upload_to_memory(index_memory_, indices.data(), static_cast<size_t>(index_size));
    }

    void create_uniform_buffer() {
        create_buffer(
            sizeof(Uniforms),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uniform_buffer_,
            uniform_memory_
        );
    }

    void create_offscreen_target() {
        require_format_features(
            offscreen_format_,
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,
            "headless offscreen target"
        );
        create_image(
            opts_.width,
            opts_.height,
            offscreen_format_,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            offscreen_image_,
            offscreen_memory_
        );
        offscreen_view_ = create_image_view(offscreen_image_, offscreen_format_);
    }

    VkFormat find_depth_format() const {
        constexpr std::array<VkFormat, 3> candidates{
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
        };
        for (VkFormat format : candidates) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physical_device_, format, &props);
            if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                return format;
            }
        }
        throw std::runtime_error("no supported depth attachment format found");
    }

    static VkImageAspectFlags depth_aspect_flags(VkFormat format) {
        VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
            flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        return flags;
    }

    void create_depth_target() {
        depth_format_ = find_depth_format();
        create_image(
            opts_.width,
            opts_.height,
            depth_format_,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            depth_image_,
            depth_memory_
        );
        depth_view_ = create_image_view(depth_image_, depth_format_, depth_aspect_flags(depth_format_));
    }

    void create_readback_buffer() {
        readback_size_ = static_cast<VkDeviceSize>(opts_.width) * opts_.height * sizeof(uint32_t);

        auto info = vk_struct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        info.size = readback_size_;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &info, nullptr, &readback_buffer_), "vkCreateBuffer readback");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, readback_buffer_, &req);

        auto alloc = vk_struct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = find_memory_type(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        check(vkAllocateMemory(device_, &alloc, nullptr, &readback_memory_), "vkAllocateMemory readback");
        check(vkBindBufferMemory(device_, readback_buffer_, readback_memory_, 0), "vkBindBufferMemory readback");
    }

    void create_swapchain_views() {
        swapchain_views_.reserve(swapchain_images_.size());
        for (VkImage image : swapchain_images_) {
            swapchain_views_.push_back(create_image_view(image, surface_format_.format));
        }
    }

    void create_render_pass() {
        target_format_ = opts_.headless ? offscreen_format_ : surface_format_.format;

        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = target_format_;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = opts_.headless ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachments[1].format = depth_format_;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_ref{};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref{};
        depth_ref.attachment = 1;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        deps[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = opts_.headless ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[1].dstAccessMask = opts_.headless ? VK_ACCESS_TRANSFER_READ_BIT : 0;

        auto info = vk_struct<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<uint32_t>(deps.size());
        info.pDependencies = deps.data();
        check(vkCreateRenderPass(device_, &info, nullptr, &render_pass_), "vkCreateRenderPass");
    }

    void create_framebuffers() {
        if (opts_.headless) {
            framebuffers_.push_back(create_framebuffer(offscreen_view_));
            return;
        }

        framebuffers_.reserve(swapchain_views_.size());
        for (VkImageView view : swapchain_views_) {
            framebuffers_.push_back(create_framebuffer(view));
        }
    }

    VkFramebuffer create_framebuffer(VkImageView color_view) {
        std::array<VkImageView, 2> attachments{color_view, depth_view_};
        auto info = vk_struct<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
        info.renderPass = render_pass_;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.width = opts_.width;
        info.height = opts_.height;
        info.layers = 1;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        check(vkCreateFramebuffer(device_, &info, nullptr, &framebuffer), "vkCreateFramebuffer");
        return framebuffer;
    }

    void create_descriptor_layouts() {
        VkDescriptorSetLayoutBinding compute_binding{};
        compute_binding.binding = 0;
        compute_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        compute_binding.descriptorCount = 1;
        compute_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        auto compute_info = vk_struct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        compute_info.bindingCount = 1;
        compute_info.pBindings = &compute_binding;
        check(vkCreateDescriptorSetLayout(device_, &compute_info, nullptr, &compute_set_layout_), "vkCreateDescriptorSetLayout compute");

        std::array<VkDescriptorSetLayoutBinding, 2> graphics_bindings{};
        graphics_bindings[0].binding = 0;
        graphics_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        graphics_bindings[0].descriptorCount = 1;
        graphics_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        graphics_bindings[1].binding = 1;
        graphics_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphics_bindings[1].descriptorCount = 1;
        graphics_bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        auto graphics_info = vk_struct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        graphics_info.bindingCount = static_cast<uint32_t>(graphics_bindings.size());
        graphics_info.pBindings = graphics_bindings.data();
        check(vkCreateDescriptorSetLayout(device_, &graphics_info, nullptr, &graphics_set_layout_), "vkCreateDescriptorSetLayout graphics");
    }

    VkShaderModule create_shader_module(const char* path) {
        auto code = read_spirv(path);
        auto info = vk_struct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        info.codeSize = code.size() * sizeof(uint32_t);
        info.pCode = code.data();

        VkShaderModule shader = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device_, &info, nullptr, &shader), "vkCreateShaderModule");
        return shader;
    }

    void create_pipelines() {
        create_compute_pipeline();
        create_graphics_pipeline();
    }

    void create_compute_pipeline() {
        auto push = VkPushConstantRange{};
        push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push.offset = 0;
        push.size = sizeof(PushConstants);

        auto layout_info = vk_struct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &compute_set_layout_;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_, &layout_info, nullptr, &compute_layout_), "vkCreatePipelineLayout compute");

        VkShaderModule shader = create_shader_module(CUBEY_COMP_SHADER_PATH);
        auto stage = vk_struct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = shader;
        stage.pName = "main";

        auto info = vk_struct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
        info.stage = stage;
        info.layout = compute_layout_;
        VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &compute_pipeline_);
        vkDestroyShaderModule(device_, shader, nullptr);
        check(result, "vkCreateComputePipelines");
    }

    void create_graphics_pipeline() {
        auto layout_info = vk_struct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &graphics_set_layout_;
        check(vkCreatePipelineLayout(device_, &layout_info, nullptr, &graphics_layout_), "vkCreatePipelineLayout graphics");

        VkShaderModule vert = create_shader_module(CUBEY_VERT_SHADER_PATH);
        VkShaderModule frag = create_shader_module(CUBEY_FRAG_SHADER_PATH);

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0] = vk_struct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1] = vk_struct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, position);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, uv);
        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, color);

        auto vertex_input = vk_struct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();

        auto input_assembly = vk_struct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        auto viewport_state = vk_struct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        auto raster = vk_struct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        auto multisample = vk_struct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        auto depth = vk_struct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        auto blend = vk_struct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        auto dynamic = vk_struct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
        dynamic.pDynamicStates = dynamic_states.data();

        auto info = vk_struct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        info.stageCount = static_cast<uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertex_input;
        info.pInputAssemblyState = &input_assembly;
        info.pViewportState = &viewport_state;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = graphics_layout_;
        info.renderPass = render_pass_;
        info.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &graphics_pipeline_);
        vkDestroyShaderModule(device_, frag, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
        check(result, "vkCreateGraphicsPipelines");
    }

    void create_descriptors() {
        std::array<VkDescriptorPoolSize, 3> sizes{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[0].descriptorCount = 1;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = 1;
        sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[2].descriptorCount = 1;

        auto pool_info = vk_struct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pool_info.maxSets = 2;
        pool_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pool_info.pPoolSizes = sizes.data();
        check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_), "vkCreateDescriptorPool");

        std::array<VkDescriptorSetLayout, 2> layouts{compute_set_layout_, graphics_set_layout_};
        auto alloc = vk_struct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        alloc.descriptorPool = descriptor_pool_;
        alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        alloc.pSetLayouts = layouts.data();

        std::array<VkDescriptorSet, 2> sets{};
        check(vkAllocateDescriptorSets(device_, &alloc, sets.data()), "vkAllocateDescriptorSets");
        compute_set_ = sets[0];
        graphics_set_ = sets[1];

        VkDescriptorImageInfo storage_image{};
        storage_image.imageView = source_view_;
        storage_image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        auto compute_write = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        compute_write.dstSet = compute_set_;
        compute_write.dstBinding = 0;
        compute_write.descriptorCount = 1;
        compute_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        compute_write.pImageInfo = &storage_image;

        VkDescriptorImageInfo sampled_image{};
        sampled_image.sampler = sampler_;
        sampled_image.imageView = source_view_;
        sampled_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        auto graphics_write = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        graphics_write.dstSet = graphics_set_;
        graphics_write.dstBinding = 0;
        graphics_write.descriptorCount = 1;
        graphics_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        graphics_write.pImageInfo = &sampled_image;

        VkDescriptorBufferInfo uniform{};
        uniform.buffer = uniform_buffer_;
        uniform.offset = 0;
        uniform.range = sizeof(Uniforms);

        auto uniform_write = vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        uniform_write.dstSet = graphics_set_;
        uniform_write.dstBinding = 1;
        uniform_write.descriptorCount = 1;
        uniform_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniform_write.pBufferInfo = &uniform;

        std::array<VkWriteDescriptorSet, 3> writes{compute_write, graphics_write, uniform_write};
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void create_commands() {
        auto pool_info = vk_struct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_;
        check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");
    }

    VkCommandBuffer allocate_command_buffer() {
        auto alloc = vk_struct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        alloc.commandPool = command_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
        return cmd;
    }

    void transition_source_to_compute(VkCommandBuffer cmd) {
        auto barrier = vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.oldLayout = source_layout_;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = source_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (source_layout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }

        vkCmdPipelineBarrier(
            cmd,
            src_stage,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
        source_layout_ = VK_IMAGE_LAYOUT_GENERAL;
    }

    void transition_source_to_sampled(VkCommandBuffer cmd) {
        auto barrier = vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = source_image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
        source_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void update_uniforms(uint32_t frame_index) {
        float aspect = static_cast<float>(opts_.width) / static_cast<float>(opts_.height);
        float t = static_cast<float>(frame_index) / 60.0f;
        Mat4 projection = perspective(radians(55.0f), aspect, 0.1f, 32.0f);
        Mat4 view = translation(0.0f, 0.0f, -4.5f);
        Mat4 model = multiply(rotation_y(t * 0.9f), rotation_x(0.55f + t * 0.35f));
        Uniforms uniforms{multiply(projection, multiply(view, model))};
        upload_to_memory(uniform_memory_, &uniforms, sizeof(uniforms));
    }

    void record_compute_and_render(VkCommandBuffer cmd, VkFramebuffer framebuffer, uint32_t frame_index, bool copy_for_readback) {
        update_uniforms(frame_index);
        transition_source_to_compute(cmd);

        PushConstants push{opts_.width, opts_.height, static_cast<float>(frame_index) / 60.0f};
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_layout_, 0, 1, &compute_set_, 0, nullptr);
        vkCmdPushConstants(cmd, compute_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &push);
        vkCmdDispatch(cmd, (opts_.width + 15) / 16, (opts_.height + 15) / 16, 1);

        transition_source_to_sampled(cmd);

        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.03f, 0.025f, 0.04f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};

        auto pass = vk_struct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
        pass.renderPass = render_pass_;
        pass.framebuffer = framebuffer;
        pass.renderArea.offset = {0, 0};
        pass.renderArea.extent = {opts_.width, opts_.height};
        pass.clearValueCount = static_cast<uint32_t>(clears.size());
        pass.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(opts_.width);
        viewport.height = static_cast<float>(opts_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, {opts_.width, opts_.height}};

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout_, 0, 1, &graphics_set_, 0, nullptr);
        VkDeviceSize vertex_offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT16);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdDrawIndexed(cmd, index_count_, 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd);

        if (copy_for_readback) {
            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {0, 0, 0};
            copy.imageExtent = {opts_.width, opts_.height, 1};
            vkCmdCopyImageToBuffer(
                cmd,
                offscreen_image_,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                readback_buffer_,
                1,
                &copy
            );

            auto barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                0,
                1,
                &barrier,
                0,
                nullptr,
                0,
                nullptr
            );
        }
    }

    void submit_and_wait(VkCommandBuffer cmd, const char* label) {
        auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        VkFence fence = VK_NULL_HANDLE;
        check(vkCreateFence(device_, &fence_info, nullptr, &fence), "vkCreateFence");

        auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        VkResult submit_result = vkQueueSubmit(queue_, 1, &submit, fence);
        if (submit_result == VK_SUCCESS) {
            submit_result = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        }

        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        check(submit_result, label);
    }

    void render_headless() {
        VkCommandBuffer cmd = allocate_command_buffer();
        auto begin = vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer headless");
        record_compute_and_render(cmd, framebuffers_[0], opts_.frames, true);
        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer headless");
        submit_and_wait(cmd, "headless graphics submit/wait");
    }

    FrameResult render_one_window_frame(uint32_t frame_index) {
        auto semaphore_info = vk_struct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkSemaphore present_ready = VK_NULL_HANDLE;
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available), "vkCreateSemaphore image_available");
        check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &present_ready), "vkCreateSemaphore present_ready");

        auto fence_info = vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        VkFence fence = VK_NULL_HANDLE;
        check(vkCreateFence(device_, &fence_info, nullptr, &fence), "vkCreateFence window");

        uint32_t image_index = 0;
        VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available, VK_NULL_HANDLE, &image_index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDestroyFence(device_, fence, nullptr);
            vkDestroySemaphore(device_, present_ready, nullptr);
            vkDestroySemaphore(device_, image_available, nullptr);
            return FrameResult::RecreateSwapchain;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            vkDestroyFence(device_, fence, nullptr);
            vkDestroySemaphore(device_, present_ready, nullptr);
            vkDestroySemaphore(device_, image_available, nullptr);
            check(acquired, "vkAcquireNextImageKHR");
        }
        bool recreate_after_present = acquired == VK_SUBOPTIMAL_KHR;

        VkCommandBuffer cmd = allocate_command_buffer();
        auto begin = vk_struct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer window");
        record_compute_and_render(cmd, framebuffers_[image_index], frame_index, false);
        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer window");

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        auto submit = vk_struct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &present_ready;
        VkResult submit_result = vkQueueSubmit(queue_, 1, &submit, fence);
        if (submit_result == VK_SUCCESS) {
            submit_result = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        }
        check(submit_result, "window graphics submit/wait");

        auto present = vk_struct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &present_ready;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index;
        VkResult present_result = vkQueuePresentKHR(queue_, &present);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
            recreate_after_present = true;
        } else if (present_result != VK_SUCCESS) {
            check(present_result, "vkQueuePresentKHR");
        }
        check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle present");

        vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
        vkDestroyFence(device_, fence, nullptr);
        vkDestroySemaphore(device_, present_ready, nullptr);
        vkDestroySemaphore(device_, image_available, nullptr);
        return recreate_after_present ? FrameResult::RecreateSwapchain : FrameResult::Rendered;
    }

    void render_window() {
        std::printf(
            "window mode: %s rendering textured cube through graphics pipeline at %ux%u\n",
            device_name(),
            opts_.width,
            opts_.height
        );

        uint32_t frame = 0;
        uint32_t consecutive_recreates = 0;
        while (!glfwWindowShouldClose(window_) && (opts_.frames == 0 || frame < opts_.frames)) {
            glfwPollEvents();
            FrameResult result = render_one_window_frame(frame);
            if (result == FrameResult::RecreateSwapchain) {
                ++consecutive_recreates;
                if (consecutive_recreates > 8) {
                    throw std::runtime_error("swapchain stayed out of date after 8 recreation attempts");
                }
                std::puts("swapchain out of date; recreating");
                recreate_window_resources();
                continue;
            }
            consecutive_recreates = 0;
            ++frame;
        }
        vkDeviceWaitIdle(device_);
    }

    std::vector<uint32_t> read_pixels() {
        std::vector<uint32_t> pixels(static_cast<size_t>(opts_.width) * opts_.height);
        void* mapped = nullptr;
        check(vkMapMemory(device_, readback_memory_, 0, readback_size_, 0, &mapped), "vkMapMemory readback");
        std::memcpy(pixels.data(), mapped, static_cast<size_t>(readback_size_));
        vkUnmapMemory(device_, readback_memory_);
        return pixels;
    }

    void verify_pixels() const {
        if (pixels_.empty()) {
            throw std::runtime_error("pixel buffer is empty");
        }

        uint32_t first = pixels_.front();
        bool varied = false;
        bool alpha_ok = true;
        for (uint32_t px : pixels_) {
            varied = varied || px != first;
            alpha_ok = alpha_ok && ((px >> 24) == 255u);
        }

        if (!varied) {
            throw std::runtime_error("render output did not vary across the image");
        }
        if (!alpha_ok) {
            throw std::runtime_error("render output alpha channel verification failed");
        }
    }

    void write_ppm() const {
        std::ofstream out(opts_.output, std::ios::binary);
        if (!out) {
            throw std::runtime_error("failed to open output image: " + opts_.output);
        }
        out << "P6\n" << opts_.width << " " << opts_.height << "\n255\n";
        for (uint32_t px : pixels_) {
            unsigned char rgb[3] = {
                static_cast<unsigned char>(px & 0xffu),
                static_cast<unsigned char>((px >> 8) & 0xffu),
                static_cast<unsigned char>((px >> 16) & 0xffu),
            };
            out.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
        }
    }

    Options opts_;
    bool glfw_initialized_ = false;
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties device_properties_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surface_format_{};
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    std::vector<VkFramebuffer> framebuffers_;

    VkFormat source_format_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkImage source_image_ = VK_NULL_HANDLE;
    VkDeviceMemory source_memory_ = VK_NULL_HANDLE;
    VkImageView source_view_ = VK_NULL_HANDLE;
    VkImageLayout source_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkFormat offscreen_format_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat target_format_ = VK_FORMAT_UNDEFINED;
    VkImage offscreen_image_ = VK_NULL_HANDLE;
    VkDeviceMemory offscreen_memory_ = VK_NULL_HANDLE;
    VkImageView offscreen_view_ = VK_NULL_HANDLE;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkBuffer readback_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    VkDeviceSize readback_size_ = 0;
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkBuffer index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_memory_ = VK_NULL_HANDLE;
    uint32_t index_count_ = 0;
    VkBuffer uniform_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniform_memory_ = VK_NULL_HANDLE;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout compute_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout graphics_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout compute_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout graphics_layout_ = VK_NULL_HANDLE;
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkDescriptorSet graphics_set_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<uint32_t> pixels_;
};

int main(int argc, char** argv) {
    try {
        Options opts = parse_args(argc, argv);
        VulkanGraphicsSpike spike(opts);
        spike.run();
        if (opts.headless) {
            std::printf(
                "graphics verification passed: %s rendered %ux%u textured cube to %s\n",
                spike.device_name(),
                opts.width,
                opts.height,
                opts.output.c_str()
            );
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cubey: %s\n", e.what());
        return 1;
    }
}
