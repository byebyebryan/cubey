#include "spinning_cube_app.h"

#include <cubey/app/windowed_host.h>
#include <cubey/spirv_io.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_SPINNING_CUBE_SHADER_DIR
#error "CUBEY_SPINNING_CUBE_SHADER_DIR must be defined by the spinning_cube CMake target"
#endif

namespace cubey::examples::spinning_cube {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr float kPi = std::numbers::pi_v<float>;

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

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_SPINNING_CUBE_SHADER_DIR) / filename;
}

struct PushConstants {
    Mat4 mvp;
};

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
};

constexpr std::array<Vertex, 24> kCubeVertices{{
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.95F, 0.25F, 0.18F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {0.95F, 0.25F, 0.18F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.95F, 0.25F, 0.18F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.95F, 0.25F, 0.18F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.18F, 0.56F, 0.95F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.18F, 0.56F, 0.95F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.18F, 0.56F, 0.95F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.18F, 0.56F, 0.95F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.22F, 0.78F, 0.42F}},
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.22F, 0.78F, 0.42F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.22F, 0.78F, 0.42F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.22F, 0.78F, 0.42F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {0.96F, 0.76F, 0.18F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.96F, 0.76F, 0.18F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.96F, 0.76F, 0.18F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.96F, 0.76F, 0.18F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.65F, 0.34F, 0.95F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.65F, 0.34F, 0.95F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.65F, 0.34F, 0.95F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.65F, 0.34F, 0.95F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.18F, 0.82F, 0.82F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.18F, 0.82F, 0.82F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {0.18F, 0.82F, 0.82F}},
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.18F, 0.82F, 0.82F}},
}};

constexpr std::array<std::uint16_t, 36> kCubeIndices{{
    0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
}};

class SpinningCubeApp {
  public:
    explicit SpinningCubeApp(RunConfig config) : config_(std::move(config)) {}

    SpinningCubeApp(const SpinningCubeApp&) = delete;
    SpinningCubeApp& operator=(const SpinningCubeApp&) = delete;

    int run() {
        if (config_.headless) {
            throw std::runtime_error("spinning_cube does not support --headless yet");
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
                        create_global_resources_if_needed(context);
                        create_swapchain_resources(context);
                    },
                .destroy_swapchain_resources =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
                .on_ready =
                    [](cubey::app::WindowedAppContext& context) {
                        std::printf("spinning_cube: %s rendering indexed cube at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update = {},
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                           std::uint32_t image_index, const FrameTiming& timing) {
                        (void)timing;
                        record_cube_frame(context, command_buffer, image_index);
                    },
                .frame_stats_sample = {},
                .shutdown =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_all_resources();
                    },
            });
        return host.run();
    }

  private:
    void create_global_resources_if_needed(cubey::app::WindowedAppContext& context) {
        if (vertex_buffer_.has_value()) {
            return;
        }
        create_cube_buffers(context);
    }

    void create_swapchain_resources(cubey::app::WindowedAppContext& context) {
        create_depth_resources(context);
        create_pipeline(context);
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_layout_.reset();
        depth_attachment_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        index_buffer_.reset();
        vertex_buffer_.reset();
    }

    void create_pipeline(cubey::app::WindowedAppContext& context) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("spinning_cube.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::read_spirv_file(shader_path("spinning_cube.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(context.device(), vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(context.device(), fragment_code);

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

        std::array<VkVertexInputAttributeDescription, 2> vertex_attributes{};
        vertex_attributes[0].location = 0;
        vertex_attributes[0].binding = 0;
        vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attributes[0].offset = offsetof(Vertex, position);
        vertex_attributes[1].location = 1;
        vertex_attributes[1].binding = 0;
        vertex_attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attributes[1].offset = offsetof(Vertex, color);

        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(PushConstants);

        auto layout_info =
            vk_struct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant_range;
        pipeline_layout_.emplace(context.device(), layout_info);

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = pipeline_layout().handle();
        pipeline_config.extent = context.swapchain().extent();
        pipeline_config.color_format = context.swapchain().format();
        pipeline_config.depth_format = depth_attachment().format();
        pipeline_config.shader_stages = shader_stages;
        pipeline_config.vertex_bindings = {&vertex_binding, 1};
        pipeline_config.vertex_attributes = vertex_attributes;
        pipeline_config.depth_test = true;
        pipeline_config.depth_write = true;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        pipeline_.emplace(context.device(), pipeline_info.create_info());
    }

    void create_depth_resources(cubey::app::WindowedAppContext& context) {
        depth_attachment_.emplace(context.device(), context.swapchain().extent());
    }

    void create_cube_buffers(cubey::app::WindowedAppContext& context) {
        const VkDeviceSize vertex_bytes =
            static_cast<VkDeviceSize>(kCubeVertices.size() * sizeof(kCubeVertices.front()));
        const VkDeviceSize index_bytes =
            static_cast<VkDeviceSize>(kCubeIndices.size() * sizeof(kCubeIndices.front()));

        vertex_buffer_ =
            cubey::vulkan::upload_device_buffer(context.device(), kCubeVertices.data(),
                                                vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        index_buffer_ = cubey::vulkan::upload_device_buffer(
            context.device(), kCubeIndices.data(), index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    [[nodiscard]] PushConstants current_push_constants(VkExtent2D extent) const {
        const auto now = std::chrono::steady_clock::now();
        const float seconds =
            static_cast<float>(std::chrono::duration<double>(now - start_time_).count());

        const Mat4 model = multiply(rotation_y(seconds * 0.9F), rotation_x(seconds * 0.55F));
        const Mat4 view = translation(0.0F, 0.0F, -4.2F);
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const Mat4 projection = perspective(kPi / 3.0F, aspect, 0.1F, 100.0F);

        return {
            multiply(projection, multiply(view, model)),
        };
    }

    void record_cube_frame(cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                           std::uint32_t image_index) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        cubey::vulkan::Swapchain& swapchain = context.swapchain();
        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain.images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));
        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_depth_attachment_transition(depth_attachment().handle()));

        VkClearValue color_clear{};
        color_clear.color = {{0.015F, 0.017F, 0.024F, 1.0F}};
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};

        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(
                swapchain.image_views().at(swapchain_image_index), color_clear);
        const VkRenderingAttachmentInfo depth_rendering_attachment =
            cubey::vulkan::depth_rendering_attachment(depth_attachment().view(), depth_clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = swapchain.extent();
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;
        rendering.pDepthAttachment = &depth_rendering_attachment;

        const PushConstants push_constants = current_push_constants(swapchain.extent());

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        const std::array<VkBuffer, 1> vertex_buffers{vertex_buffer().handle()};
        constexpr std::array<VkDeviceSize, 1> vertex_offsets{0};
        vkCmdBindVertexBuffers(command_buffer, 0, static_cast<std::uint32_t>(vertex_buffers.size()),
                               vertex_buffers.data(), vertex_offsets.data());
        vkCmdBindIndexBuffer(command_buffer, index_buffer().handle(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(command_buffer, pipeline_layout().handle(), VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(PushConstants), &push_constants);
        vkCmdDrawIndexed(command_buffer, static_cast<std::uint32_t>(kCubeIndices.size()), 1, 0, 0,
                         0);
        vkCmdEndRendering(command_buffer);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer spinning_cube");
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

    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const {
        if (!depth_attachment_.has_value()) {
            throw std::runtime_error("depth attachment is not initialized");
        }
        return depth_attachment_.value();
    }

    RunConfig config_;
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();

    std::optional<cubey::vulkan::Buffer> vertex_buffer_;
    std::optional<cubey::vulkan::Buffer> index_buffer_;
    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
};

} // namespace

int run_spinning_cube(const RunConfig& config) {
    SpinningCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::spinning_cube
