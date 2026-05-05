#include "textured_cube_app.h"

#include <cubey/frame_clock.h>
#include <cubey/frame_stats.h>
#include <cubey/orbit_controller.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/render_context.h>
#include <cubey/vulkan/rendering.h>
#include <cubey/vulkan/sampler.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/swapchain.h>
#include <cubey/vulkan/vk_check.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_TEXTURED_CUBE_SHADER_DIR
#error "CUBEY_TEXTURED_CUBE_SHADER_DIR must be defined by the textured_cube CMake target"
#endif

namespace cubey::examples::textured_cube {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr std::uint32_t kTextureWidth = 64;
constexpr std::uint32_t kTextureHeight = 64;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kTextureComputeGroupSize = 8;

struct Mat4 {
    std::array<float, 16> values{};
};

float& at(Mat4& matrix, std::size_t row, std::size_t column) {
    return matrix.values[(column * 4U) + row];
}

float at(const Mat4& matrix, std::size_t row, std::size_t column) {
    return matrix.values[(column * 4U) + row];
}

Mat4 identity() {
    Mat4 matrix{};
    at(matrix, 0, 0) = 1.0F;
    at(matrix, 1, 1) = 1.0F;
    at(matrix, 2, 2) = 1.0F;
    at(matrix, 3, 3) = 1.0F;
    return matrix;
}

Mat4 multiply(const Mat4& lhs, const Mat4& rhs) {
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float value = 0.0F;
            for (std::size_t i = 0; i < 4; ++i) {
                value += at(lhs, row, i) * at(rhs, i, column);
            }
            at(result, row, column) = value;
        }
    }
    return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Mat4 translation(float x, float y, float z) {
    Mat4 matrix = identity();
    at(matrix, 0, 3) = x;
    at(matrix, 1, 3) = y;
    at(matrix, 2, 3) = z;
    return matrix;
}

Mat4 rotation_x(float angle) {
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);

    Mat4 matrix = identity();
    at(matrix, 1, 1) = cosine;
    at(matrix, 1, 2) = -sine;
    at(matrix, 2, 1) = sine;
    at(matrix, 2, 2) = cosine;
    return matrix;
}

Mat4 rotation_y(float angle) {
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);

    Mat4 matrix = identity();
    at(matrix, 0, 0) = cosine;
    at(matrix, 0, 2) = sine;
    at(matrix, 2, 0) = -sine;
    at(matrix, 2, 2) = cosine;
    return matrix;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z) {
    const float focal = 1.0F / std::tan(fovy_radians * 0.5F);

    Mat4 matrix{};
    at(matrix, 0, 0) = focal / aspect;
    at(matrix, 1, 1) = -focal;
    at(matrix, 2, 2) = far_z / (near_z - far_z);
    at(matrix, 2, 3) = (far_z * near_z) / (near_z - far_z);
    at(matrix, 3, 2) = -1.0F;
    return matrix;
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
    return std::filesystem::path(CUBEY_TEXTURED_CUBE_SHADER_DIR) / filename;
}

struct SceneUniforms {
    Mat4 mvp;
    Mat4 model;
    std::array<float, 4> light_direction;
    std::array<float, 4> light_color;
    std::array<float, 4> ambient_color;
};

static_assert(sizeof(SceneUniforms) == (sizeof(Mat4) * 2U) + (sizeof(float) * 12U));

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
    std::array<float, 3> normal;
    std::array<float, 2> uv;
};

constexpr std::array<Vertex, 24> kCubeVertices{{
    Vertex{{-1.0F, -1.0F, 1.0F}, {1.0F, 0.92F, 0.86F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {1.0F, 0.92F, 0.86F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {1.0F, 0.92F, 0.86F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {1.0F, 0.92F, 0.86F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.86F, 0.94F, 1.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.86F, 0.94F, 1.0F}, {0.0F, 0.0F, -1.0F}, {1.0F, 0.0F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.86F, 0.94F, 1.0F}, {0.0F, 0.0F, -1.0F}, {1.0F, 1.0F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.86F, 0.94F, 1.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.9F, 1.0F, 0.9F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.9F, 1.0F, 0.9F}, {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.9F, 1.0F, 0.9F}, {-1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.9F, 1.0F, 0.9F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {1.0F, 0.96F, 0.78F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {1.0F, 0.96F, 0.78F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {1.0F, 0.96F, 0.78F}, {1.0F, 0.0F, 0.0F}, {1.0F, 1.0F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {1.0F, 0.96F, 0.78F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.96F, 0.9F, 1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.96F, 0.9F, 1.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.96F, 0.9F, 1.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.96F, 0.9F, 1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.86F, 1.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.86F, 1.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {1.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {0.86F, 1.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {1.0F, 1.0F}},
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.86F, 1.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, 1.0F}},
}};

constexpr std::array<std::uint16_t, 36> kCubeIndices{{
    0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
}};

class TexturedCubeApp {
  public:
    explicit TexturedCubeApp(RunConfig config) : config_(std::move(config)) {}

    TexturedCubeApp(const TexturedCubeApp&) = delete;
    TexturedCubeApp& operator=(const TexturedCubeApp&) = delete;

    ~TexturedCubeApp() {
        if (device_ != VK_NULL_HANDLE) {
            static_cast<void>(vkDeviceWaitIdle(device_));
        }

        frame_resources_.reset();
        destroy_swapchain_resources();
        destroy_descriptors();
        destroy_compute_resources();
        texture_sampler_.reset();
        texture_image_.reset();
        scene_uniform_buffer_.reset();
        index_buffer_.reset();
        vertex_buffer_.reset();

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
            throw std::runtime_error("textured_cube does not support --headless yet");
        }

        init_window();
        create_instance();
        create_surface();
        create_device();
        create_cube_buffers();
        create_scene_uniform_buffer();
        create_texture_resources();
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
        glfwSetCursorPosCallback(window_, cursor_pos_callback);
        glfwSetMouseButtonCallback(window_, mouse_button_callback);
        glfwSetKeyCallback(window_, key_callback);
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void framebuffer_size_callback(GLFWwindow* window, int unused_width, int unused_height) {
        (void)unused_width;
        (void)unused_height;
        auto* app = static_cast<TexturedCubeApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->framebuffer_resized_ = true;
        }
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void cursor_pos_callback(GLFWwindow* window, double x, double y) {
        auto* app = static_cast<TexturedCubeApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr && app->orbit_controller_.dragging()) {
            app->orbit_controller_.drag_to(x, y);
        }
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int unused_mods) {
        (void)unused_mods;
        auto* app = static_cast<TexturedCubeApp*>(glfwGetWindowUserPointer(window));
        if (app == nullptr || button != GLFW_MOUSE_BUTTON_LEFT) {
            return;
        }

        if (action == GLFW_PRESS) {
            double x = 0.0;
            double y = 0.0;
            glfwGetCursorPos(window, &x, &y);
            app->orbit_controller_.begin_drag(x, y);
        } else if (action == GLFW_RELEASE) {
            app->orbit_controller_.end_drag();
        }
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void key_callback(GLFWwindow* window, int key, int unused_scancode, int action,
                             int unused_mods) {
        (void)unused_scancode;
        (void)unused_mods;
        auto* app = static_cast<TexturedCubeApp*>(glfwGetWindowUserPointer(window));
        if (app == nullptr || action != GLFW_PRESS) {
            return;
        }

        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        } else if (key == GLFW_KEY_R) {
            app->orbit_controller_.reset();
            app->reset_frame_timing();
        } else if (key == GLFW_KEY_SPACE) {
            app->orbit_controller_.toggle_pause();
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
        device_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
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
        create_depth_resources();
        create_pipeline();
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_layout_.reset();
        depth_attachment_.reset();
        swapchain_.reset();
    }

    void recreate_swapchain_resources() {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle before swapchain recreate");
        frame_resources_.reset();
        destroy_swapchain_resources();
        create_swapchain_resources();
        create_frame_resources();
    }

    void reset_frame_timing() {
        frame_clock_.reset();
        frame_stats_.reset();
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
            read_spirv_file(shader_path("textured_cube.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            read_spirv_file(shader_path("textured_cube.frag.spv"));
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

        VkVertexInputBindingDescription vertex_binding{};
        vertex_binding.binding = 0;
        vertex_binding.stride = sizeof(Vertex);
        vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 4> vertex_attributes{};
        vertex_attributes[0].location = 0;
        vertex_attributes[0].binding = 0;
        vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attributes[0].offset = offsetof(Vertex, position);
        vertex_attributes[1].location = 1;
        vertex_attributes[1].binding = 0;
        vertex_attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attributes[1].offset = offsetof(Vertex, color);
        vertex_attributes[2].location = 2;
        vertex_attributes[2].binding = 0;
        vertex_attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attributes[2].offset = offsetof(Vertex, normal);
        vertex_attributes[3].location = 3;
        vertex_attributes[3].binding = 0;
        vertex_attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
        vertex_attributes[3].offset = offsetof(Vertex, uv);

        const std::array<VkDescriptorSetLayout, 1> set_layouts{
            descriptor_set_layout().handle(),
        };
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = {},
        });
        pipeline_layout_.emplace(vulkan_device(), layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = pipeline_layout().handle();
        pipeline_config.extent = swapchain().extent();
        pipeline_config.color_format = swapchain().format();
        pipeline_config.depth_format = depth_attachment().format();
        pipeline_config.shader_stages = shader_stages;
        pipeline_config.vertex_bindings = {&vertex_binding, 1};
        pipeline_config.vertex_attributes = vertex_attributes;
        pipeline_config.depth_test = true;
        pipeline_config.depth_write = true;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        pipeline_.emplace(vulkan_device(), pipeline_info.create_info());
    }

    void create_depth_resources() {
        depth_attachment_.emplace(vulkan_device(), swapchain().extent());
    }

    void create_frame_resources() {
        frame_resources_.emplace(vulkan_device(), swapchain().image_count());
    }

    void create_cube_buffers() {
        const VkDeviceSize vertex_bytes =
            static_cast<VkDeviceSize>(kCubeVertices.size() * sizeof(kCubeVertices.front()));
        const VkDeviceSize index_bytes =
            static_cast<VkDeviceSize>(kCubeIndices.size() * sizeof(kCubeIndices.front()));

        vertex_buffer_ = cubey::vulkan::upload_device_buffer(
            vulkan_device(), kCubeVertices.data(), vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        index_buffer_ = cubey::vulkan::upload_device_buffer(
            vulkan_device(), kCubeIndices.data(), index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    void create_scene_uniform_buffer() {
        cubey::vulkan::BufferConfig config;
        config.size = sizeof(SceneUniforms);
        config.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        config.memory_properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        scene_uniform_buffer_.emplace(vulkan_device(), config);
    }

    void create_texture_resources() {
        validate_texture_format_support();

        const cubey::vulkan::ImageConfig image_config = cubey::vulkan::storage_sampled_image_config(
            {kTextureWidth, kTextureHeight}, kTextureFormat);
        texture_image_.emplace(vulkan_device(), image_config);

        create_compute_resources();
        dispatch_compute_texture();
        destroy_compute_resources();

        cubey::vulkan::SamplerConfig sampler_config;
        texture_sampler_.emplace(vulkan_device(), sampler_config);
        create_descriptors();
    }

    void validate_texture_format_support() const {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(vulkan_device().physical_device(), kTextureFormat,
                                            &properties);
        constexpr VkFormatFeatureFlags required_features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                           VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if ((properties.optimalTilingFeatures & required_features) != required_features) {
            throw std::runtime_error(
                "texture format does not support storage-image generation, sampling, and readback");
        }
    }

    void transition_texture_image(const cubey::vulkan::ImageLayoutTransition& transition) const {
        cubey::vulkan::ImmediateCommands commands(vulkan_device());
        cubey::vulkan::transition_image_layout(commands.command_buffer(), transition);
        commands.submit_and_wait();
    }

    void create_compute_resources() {
        const std::array<VkDescriptorSetLayoutBinding, 1> bindings{
            cubey::vulkan::descriptor_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                              VK_SHADER_STAGE_COMPUTE_BIT),
        };
        const VkDescriptorSetLayoutCreateInfo descriptor_layout_info =
            cubey::vulkan::descriptor_set_layout_info(bindings);
        compute_descriptor_set_layout_.emplace(vulkan_device(), descriptor_layout_info);

        const std::array<VkDescriptorPoolSize, 1> pool_sizes{
            cubey::vulkan::descriptor_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1),
        };
        const VkDescriptorPoolCreateInfo descriptor_pool_info =
            cubey::vulkan::descriptor_pool_info(1, pool_sizes);
        compute_descriptor_pool_.emplace(vulkan_device(), descriptor_pool_info);

        compute_descriptor_set_ =
            compute_descriptor_pool().allocate(compute_descriptor_set_layout().handle());

        const cubey::vulkan::DescriptorImageWrite texture_write =
            cubey::vulkan::storage_image_descriptor(compute_descriptor_set_, 0,
                                                    texture_image().view());
        const std::array<VkWriteDescriptorSet, 1> writes{texture_write.descriptor_write()};
        cubey::vulkan::update_descriptor_sets(vulkan_device(), writes);

        const std::array<VkDescriptorSetLayout, 1> set_layouts{
            compute_descriptor_set_layout().handle(),
        };
        const cubey::vulkan::PipelineLayoutInfo pipeline_layout_info({
            .set_layouts = set_layouts,
            .push_constants = {},
        });
        compute_pipeline_layout_.emplace(vulkan_device(), pipeline_layout_info.create_info());

        const std::vector<std::uint32_t> compute_code =
            read_spirv_file(shader_path("textured_cube.comp.spv"));
        cubey::vulkan::ShaderModule compute_shader(vulkan_device(), compute_code);

        const VkPipelineShaderStageCreateInfo stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, compute_shader.handle());
        const cubey::vulkan::ComputePipelineInfo pipeline_info({
            .layout = compute_pipeline_layout().handle(),
            .shader_stage = stage,
        });
        compute_pipeline_.emplace(vulkan_device(), pipeline_info.create_info());
    }

    void destroy_compute_resources() {
        compute_pipeline_.reset();
        compute_pipeline_layout_.reset();
        compute_descriptor_set_ = VK_NULL_HANDLE;
        compute_descriptor_pool_.reset();
        compute_descriptor_set_layout_.reset();
    }

    void dispatch_compute_texture() const {
        transition_texture_image(
            cubey::vulkan::begin_storage_image_write_transition(texture_image().handle()));

        cubey::vulkan::ImmediateCommands commands(vulkan_device());
        vkCmdBindPipeline(commands.command_buffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                          compute_pipeline().handle());
        vkCmdBindDescriptorSets(commands.command_buffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute_pipeline_layout().handle(), 0, 1, &compute_descriptor_set_,
                                0, nullptr);
        constexpr std::uint32_t groups_x =
            (kTextureWidth + kTextureComputeGroupSize - 1U) / kTextureComputeGroupSize;
        constexpr std::uint32_t groups_y =
            (kTextureHeight + kTextureComputeGroupSize - 1U) / kTextureComputeGroupSize;
        vkCmdDispatch(commands.command_buffer(), groups_x, groups_y, 1);
        commands.submit_and_wait();

        transition_texture_image(cubey::vulkan::finish_storage_image_write_for_sampling_transition(
            texture_image().handle()));
    }

    void create_descriptors() {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            cubey::vulkan::descriptor_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                              VK_SHADER_STAGE_VERTEX_BIT |
                                                  VK_SHADER_STAGE_FRAGMENT_BIT),
            cubey::vulkan::descriptor_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                              VK_SHADER_STAGE_FRAGMENT_BIT),
        };

        const VkDescriptorSetLayoutCreateInfo layout_info =
            cubey::vulkan::descriptor_set_layout_info(bindings);
        descriptor_set_layout_.emplace(vulkan_device(), layout_info);

        const std::array<VkDescriptorPoolSize, 2> pool_sizes{{
            cubey::vulkan::descriptor_pool_size(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
            cubey::vulkan::descriptor_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1),
        }};

        const VkDescriptorPoolCreateInfo pool_info =
            cubey::vulkan::descriptor_pool_info(1, pool_sizes);
        descriptor_pool_.emplace(vulkan_device(), pool_info);
        descriptor_set_ = descriptor_pool_->allocate(descriptor_set_layout().handle());

        const cubey::vulkan::DescriptorBufferWrite scene_write =
            cubey::vulkan::uniform_buffer_descriptor(
                descriptor_set_, 0, scene_uniform_buffer().handle(), sizeof(SceneUniforms));
        const cubey::vulkan::DescriptorImageWrite image_write =
            cubey::vulkan::combined_image_sampler_descriptor(
                descriptor_set_, 1, texture_sampler().handle(), texture_image().view());
        const std::array<VkWriteDescriptorSet, 2> writes{
            scene_write.descriptor_write(),
            image_write.descriptor_write(),
        };
        cubey::vulkan::update_descriptor_sets(vulkan_device(), writes);
    }

    void destroy_descriptors() {
        descriptor_set_ = VK_NULL_HANDLE;
        descriptor_pool_.reset();
        descriptor_set_layout_.reset();
    }

    [[nodiscard]] SceneUniforms current_scene_uniforms() const {
        const Mat4 model =
            multiply(rotation_y(orbit_controller_.yaw()), rotation_x(orbit_controller_.pitch()));
        const Mat4 view = translation(0.0F, 0.0F, -4.2F);
        const float aspect = static_cast<float>(swapchain().extent().width) /
                             static_cast<float>(swapchain().extent().height);
        const Mat4 projection = perspective(kPi / 3.0F, aspect, 0.1F, 100.0F);

        return {
            .mvp = multiply(projection, multiply(view, model)),
            .model = model,
            .light_direction = {0.35F, -0.55F, 0.76F, 0.0F},
            .light_color = {0.76F, 0.76F, 0.76F, 1.0F},
            .ambient_color = {0.24F, 0.24F, 0.24F, 1.0F},
        };
    }

    void update_scene_uniforms() {
        const SceneUniforms uniforms = current_scene_uniforms();
        scene_uniform_buffer().upload(&uniforms, sizeof(uniforms));
    }

    void record_cube_frame(VkCommandBuffer command_buffer, std::uint32_t image_index) {
        update_scene_uniforms();

        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain().images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));
        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_depth_attachment_transition(depth_attachment().handle()));

        VkClearValue color_clear{};
        color_clear.color = {{0.014F, 0.016F, 0.022F, 1.0F}};
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};

        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(
                swapchain().image_views().at(swapchain_image_index), color_clear);
        const VkRenderingAttachmentInfo depth_rendering_attachment =
            cubey::vulkan::depth_rendering_attachment(depth_attachment().view(), depth_clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = swapchain().extent();
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;
        rendering.pDepthAttachment = &depth_rendering_attachment;

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout().handle(), 0, 1, &descriptor_set_, 0, nullptr);
        const std::array<VkBuffer, 1> vertex_buffers{vertex_buffer().handle()};
        constexpr std::array<VkDeviceSize, 1> vertex_offsets{0};
        vkCmdBindVertexBuffers(command_buffer, 0, static_cast<std::uint32_t>(vertex_buffers.size()),
                               vertex_buffers.data(), vertex_offsets.data());
        vkCmdBindIndexBuffer(command_buffer, index_buffer().handle(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(command_buffer, static_cast<std::uint32_t>(kCubeIndices.size()), 1, 0, 0,
                         0);
        vkCmdEndRendering(command_buffer);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer textured_cube");
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

        record_cube_frame(frame.command_buffer, frame.image_index);
        return render_context.end_frame(frame);
    }

    void render_window() {
        orbit_controller_.set_auto_rotation_speed(0.9F);
        reset_frame_timing();

        std::printf(
            "textured_cube: %s rendering interactive compute shaded textured cube at %ux%u\n",
            vulkan_device().device_name(), swapchain().extent().width, swapchain().extent().height);

        std::uint32_t frame = 0;
        cubey::vulkan::SwapchainRecreateTracker recreate_tracker;
        while (glfwWindowShouldClose(window_) == 0 &&
               (config_.frames == 0 || frame < config_.frames)) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window_) != 0) {
                break;
            }

            if (framebuffer_resized_) {
                std::puts("framebuffer resized; recreating swapchain");
                recreate_swapchain_resources();
                reset_frame_timing();
                recreate_tracker.reset();
                continue;
            }

            const FrameTiming timing = frame_clock_.tick();
            orbit_controller_.update(timing.delta_seconds);

            cubey::vulkan::RenderFrameResult result = draw_frame();
            if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
                recreate_tracker.record_recreate_request();
                std::puts("swapchain out of date; recreating");
                recreate_swapchain_resources();
                reset_frame_timing();
                continue;
            }

            recreate_tracker.reset();
            const VkExtent2D extent = swapchain().extent();
            std::optional<FrameStatsSnapshot> stats = frame_stats_.record_frame({
                .delta_seconds = timing.delta_seconds,
                .width = extent.width,
                .height = extent.height,
                .triangles = static_cast<std::uint32_t>(kCubeIndices.size() / 3U),
            });
            if (stats.has_value()) {
                const std::string title = format_window_title(config_.title, stats.value());
                glfwSetWindowTitle(window_, title.c_str());
            }
            ++frame;
        }

        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle after textured_cube");
    }

    [[nodiscard]] cubey::vulkan::Swapchain& swapchain() {
        if (!swapchain_.has_value()) {
            throw std::runtime_error("swapchain is not initialized");
        }
        return swapchain_.value();
    }

    [[nodiscard]] const cubey::vulkan::Swapchain& swapchain() const {
        if (!swapchain_.has_value()) {
            throw std::runtime_error("swapchain is not initialized");
        }
        return swapchain_.value();
    }

    [[nodiscard]] cubey::vulkan::Device& vulkan_device() {
        if (!device_owner_.has_value()) {
            throw std::runtime_error("Vulkan device is not initialized");
        }
        return device_owner_.value();
    }

    [[nodiscard]] const cubey::vulkan::Device& vulkan_device() const {
        if (!device_owner_.has_value()) {
            throw std::runtime_error("Vulkan device is not initialized");
        }
        return device_owner_.value();
    }

    [[nodiscard]] cubey::vulkan::FrameResources& frame_resources() {
        if (!frame_resources_.has_value()) {
            throw std::runtime_error("frame resources are not initialized");
        }
        return frame_resources_.value();
    }

    [[nodiscard]] cubey::vulkan::Buffer& vertex_buffer() {
        if (!vertex_buffer_.has_value()) {
            throw std::runtime_error("vertex buffer is not initialized");
        }
        return vertex_buffer_.value();
    }

    [[nodiscard]] cubey::vulkan::Buffer& index_buffer() {
        if (!index_buffer_.has_value()) {
            throw std::runtime_error("index buffer is not initialized");
        }
        return index_buffer_.value();
    }

    [[nodiscard]] cubey::vulkan::Buffer& scene_uniform_buffer() {
        if (!scene_uniform_buffer_.has_value()) {
            throw std::runtime_error("scene uniform buffer is not initialized");
        }
        return scene_uniform_buffer_.value();
    }

    [[nodiscard]] const cubey::vulkan::Image& texture_image() const {
        if (!texture_image_.has_value()) {
            throw std::runtime_error("texture image is not initialized");
        }
        return texture_image_.value();
    }

    [[nodiscard]] const cubey::vulkan::Sampler& texture_sampler() const {
        if (!texture_sampler_.has_value()) {
            throw std::runtime_error("texture sampler is not initialized");
        }
        return texture_sampler_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetLayout& descriptor_set_layout() const {
        if (!descriptor_set_layout_.has_value()) {
            throw std::runtime_error("descriptor set layout is not initialized");
        }
        return descriptor_set_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetLayout& compute_descriptor_set_layout() const {
        if (!compute_descriptor_set_layout_.has_value()) {
            throw std::runtime_error("compute descriptor set layout is not initialized");
        }
        return compute_descriptor_set_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorPool& compute_descriptor_pool() const {
        if (!compute_descriptor_pool_.has_value()) {
            throw std::runtime_error("compute descriptor pool is not initialized");
        }
        return compute_descriptor_pool_.value();
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

    [[nodiscard]] const cubey::vulkan::PipelineLayout& compute_pipeline_layout() const {
        if (!compute_pipeline_layout_.has_value()) {
            throw std::runtime_error("compute pipeline layout is not initialized");
        }
        return compute_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::ComputePipeline& compute_pipeline() const {
        if (!compute_pipeline_.has_value()) {
            throw std::runtime_error("compute pipeline is not initialized");
        }
        return compute_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const {
        if (!depth_attachment_.has_value()) {
            throw std::runtime_error("depth attachment is not initialized");
        }
        return depth_attachment_.value();
    }

    RunConfig config_;
    bool glfw_initialized_ = false;
    bool framebuffer_resized_ = false;
    GLFWwindow* window_ = nullptr;
    FrameClock frame_clock_;
    FrameStats frame_stats_;
    OrbitController orbit_controller_;

    std::optional<cubey::vulkan::Instance> instance_owner_;
    std::optional<cubey::vulkan::Device> device_owner_;
    std::optional<cubey::vulkan::Swapchain> swapchain_;
    std::optional<cubey::vulkan::FrameResources> frame_resources_;
    std::optional<cubey::vulkan::Buffer> vertex_buffer_;
    std::optional<cubey::vulkan::Buffer> index_buffer_;
    std::optional<cubey::vulkan::Buffer> scene_uniform_buffer_;
    std::optional<cubey::vulkan::Image> texture_image_;
    std::optional<cubey::vulkan::Sampler> texture_sampler_;
    std::optional<cubey::vulkan::DescriptorSetLayout> descriptor_set_layout_;
    std::optional<cubey::vulkan::DescriptorPool> descriptor_pool_;
    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
    std::optional<cubey::vulkan::DescriptorSetLayout> compute_descriptor_set_layout_;
    std::optional<cubey::vulkan::DescriptorPool> compute_descriptor_pool_;
    std::optional<cubey::vulkan::PipelineLayout> compute_pipeline_layout_;
    std::optional<cubey::vulkan::ComputePipeline> compute_pipeline_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_descriptor_set_ = VK_NULL_HANDLE;
};

} // namespace

int run_textured_cube(const RunConfig& config) {
    TexturedCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::textured_cube
