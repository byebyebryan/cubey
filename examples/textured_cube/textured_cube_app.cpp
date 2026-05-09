#include "textured_cube_app.h"

#include <cubey/app/glfw_window.h>
#include <cubey/app/windowed_host.h>
#include <cubey/frame_stats.h>
#include <cubey/math.h>
#include <cubey/orbit_camera_3d.h>
#include <cubey/orbit_controller.h>
#include <cubey/render/target.h>
#include <cubey/spirv_io.h>
#include <cubey/transform_3d.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/sampler.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_TEXTURED_CUBE_SHADER_DIR
#error "CUBEY_TEXTURED_CUBE_SHADER_DIR must be defined by the textured_cube CMake target"
#endif

namespace cubey::examples::textured_cube {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr std::uint32_t kTextureWidth = 64;
constexpr std::uint32_t kTextureHeight = 64;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kTextureComputeGroupSize = 8;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TEXTURED_CUBE_SHADER_DIR) / filename;
}

struct SceneUniforms {
    cubey::math::Mat4 mvp;
    cubey::math::Mat4 model;
    std::array<float, 4> light_direction;
    std::array<float, 4> light_color;
    std::array<float, 4> ambient_color;
};

static_assert(sizeof(cubey::math::Mat4) == sizeof(float) * 16U);
static_assert(sizeof(SceneUniforms) == (sizeof(cubey::math::Mat4) * 2U) + (sizeof(float) * 12U));

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

    int run() {
        if (config_.headless) {
            throw std::runtime_error("textured_cube does not support --headless yet");
        }

        cubey::app::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
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
                    [this](cubey::app::WindowedAppContext& context) {
                        orbit_controller_.set_auto_rotation_speed(0.9F);
                        std::printf("textured_cube: %s rendering interactive compute shaded "
                                    "textured cube at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update =
                    [this](cubey::app::WindowedAppContext& context, const FrameTiming& timing) {
                        if (context.input().key_pressed(cubey::input::Key::Escape)) {
                            context.window().request_close();
                        }
                        orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
                    },
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context,
                           const cubey::app::WindowedRenderFrame& frame) {
                        (void)context;
                        record_cube_frame(frame);
                    },
                .frame_stats_sample =
                    [](cubey::app::WindowedAppContext& context,
                       const FrameTiming& timing) -> std::optional<FrameStatsSample> {
                    const VkExtent2D extent = context.swapchain().extent();
                    return FrameStatsSample{
                        .delta_seconds = timing.delta_seconds,
                        .width = extent.width,
                        .height = extent.height,
                        .triangles = static_cast<std::uint32_t>(kCubeIndices.size() / 3U),
                    };
                },
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
        create_scene_uniform_buffer(context);
        create_texture_resources(context);
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
        destroy_descriptors();
        destroy_compute_resources();
        texture_sampler_.reset();
        texture_image_.reset();
        scene_uniform_buffer_.reset();
        index_buffer_.reset();
        vertex_buffer_.reset();
    }

    void create_pipeline(cubey::app::WindowedAppContext& context) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("textured_cube.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::read_spirv_file(shader_path("textured_cube.frag.spv"));
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

        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = {},
        });
        pipeline_layout_.emplace(context.device(), layout_info.create_info());

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

    void create_scene_uniform_buffer(cubey::app::WindowedAppContext& context) {
        cubey::vulkan::BufferConfig config;
        config.size = sizeof(SceneUniforms);
        config.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        config.memory_properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        scene_uniform_buffer_.emplace(context.device(), config);
    }

    void create_texture_resources(cubey::app::WindowedAppContext& context) {
        validate_texture_format_support(context);

        const cubey::vulkan::ImageConfig image_config = cubey::vulkan::storage_sampled_image_config(
            {kTextureWidth, kTextureHeight}, kTextureFormat);
        texture_image_.emplace(context.device(), image_config);

        create_compute_resources(context);
        dispatch_compute_texture(context);
        destroy_compute_resources();

        cubey::vulkan::SamplerConfig sampler_config;
        texture_sampler_.emplace(context.device(), sampler_config);
        create_descriptors(context);
    }

    static void validate_texture_format_support(cubey::app::WindowedAppContext& context) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context.device().physical_device(), kTextureFormat,
                                            &properties);
        constexpr VkFormatFeatureFlags required_features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                           VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if ((properties.optimalTilingFeatures & required_features) != required_features) {
            throw std::runtime_error(
                "texture format does not support storage-image generation, sampling, and readback");
        }
    }

    static void transition_texture_image(cubey::app::WindowedAppContext& context,
                                         const cubey::vulkan::ImageLayoutTransition& transition) {
        cubey::vulkan::ImmediateCommands commands(context.device());
        cubey::vulkan::transition_image_layout(commands.command_buffer(), transition);
        commands.submit_and_wait();
    }

    void create_compute_resources(cubey::app::WindowedAppContext& context) {
        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
        compute_descriptors_.emplace(context.device(), descriptor_info);

        const cubey::vulkan::DescriptorImageWrite texture_write =
            cubey::vulkan::storage_image_descriptor(compute_descriptors().set(), 0,
                                                    texture_image().view());
        const std::array<VkWriteDescriptorSet, 1> writes{texture_write.descriptor_write()};
        cubey::vulkan::update_descriptor_sets(context.device(), writes);

        const std::array<VkDescriptorSetLayout, 1> set_layouts{compute_descriptors().layout()};
        const cubey::vulkan::PipelineLayoutInfo pipeline_layout_info({
            .set_layouts = set_layouts,
            .push_constants = {},
        });
        compute_pipeline_layout_.emplace(context.device(), pipeline_layout_info.create_info());

        const std::vector<std::uint32_t> compute_code =
            cubey::read_spirv_file(shader_path("textured_cube.comp.spv"));
        cubey::vulkan::ShaderModule compute_shader(context.device(), compute_code);

        const VkPipelineShaderStageCreateInfo stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, compute_shader.handle());
        const cubey::vulkan::ComputePipelineInfo pipeline_info({
            .layout = compute_pipeline_layout().handle(),
            .shader_stage = stage,
        });
        compute_pipeline_.emplace(context.device(), pipeline_info.create_info());
    }

    void destroy_compute_resources() {
        compute_pipeline_.reset();
        compute_pipeline_layout_.reset();
        compute_descriptors_.reset();
    }

    void dispatch_compute_texture(cubey::app::WindowedAppContext& context) const {
        transition_texture_image(
            context, cubey::vulkan::begin_storage_image_write_transition(texture_image().handle()));

        cubey::vulkan::ImmediateCommands commands(context.device());
        vkCmdBindPipeline(commands.command_buffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                          compute_pipeline().handle());
        const VkDescriptorSet descriptor_set = compute_descriptors().set();
        vkCmdBindDescriptorSets(commands.command_buffer(), VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute_pipeline_layout().handle(), 0, 1, &descriptor_set, 0,
                                nullptr);
        constexpr std::uint32_t groups_x =
            (kTextureWidth + kTextureComputeGroupSize - 1U) / kTextureComputeGroupSize;
        constexpr std::uint32_t groups_y =
            (kTextureHeight + kTextureComputeGroupSize - 1U) / kTextureComputeGroupSize;
        vkCmdDispatch(commands.command_buffer(), groups_x, groups_y, 1);
        commands.submit_and_wait();

        transition_texture_image(context,
                                 cubey::vulkan::finish_storage_image_write_for_sampling_transition(
                                     texture_image().handle()));
    }

    void create_descriptors(cubey::app::WindowedAppContext& context) {
        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 1,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
        descriptors_.emplace(context.device(), descriptor_info);

        const cubey::vulkan::DescriptorBufferWrite scene_write =
            cubey::vulkan::uniform_buffer_descriptor(
                descriptors().set(), 0, scene_uniform_buffer().handle(), sizeof(SceneUniforms));
        const cubey::vulkan::DescriptorImageWrite image_write =
            cubey::vulkan::combined_image_sampler_descriptor(
                descriptors().set(), 1, texture_sampler().handle(), texture_image().view());
        const std::array<VkWriteDescriptorSet, 2> writes{
            scene_write.descriptor_write(),
            image_write.descriptor_write(),
        };
        cubey::vulkan::update_descriptor_sets(context.device(), writes);
    }

    void destroy_descriptors() {
        descriptors_.reset();
    }

    [[nodiscard]] SceneUniforms current_scene_uniforms(VkExtent2D extent) const {
        const cubey::Transform3D transform{
            .rotation_radians = {orbit_controller_.pitch(), orbit_controller_.yaw(), 0.0F},
        };
        const cubey::math::Mat4 model = transform.model_matrix();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

        return {
            .mvp = camera_.view_projection_matrix(aspect) * model,
            .model = model,
            .light_direction = {0.35F, -0.55F, 0.76F, 0.0F},
            .light_color = {0.76F, 0.76F, 0.76F, 1.0F},
            .ambient_color = {0.24F, 0.24F, 0.24F, 1.0F},
        };
    }

    void update_scene_uniforms(VkExtent2D extent) {
        const SceneUniforms uniforms = current_scene_uniforms(extent);
        scene_uniform_buffer().upload(&uniforms, sizeof(uniforms));
    }

    void record_cube_frame(const cubey::app::WindowedRenderFrame& frame) {
        const VkCommandBuffer command_buffer = frame.command_buffer;
        update_scene_uniforms(frame.color_target.extent);

        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_color_attachment_transition(frame.color_target.image));
        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_depth_attachment_transition(depth_attachment().handle()));

        VkClearValue color_clear{};
        color_clear.color = {{0.014F, 0.016F, 0.022F, 1.0F}};
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};
        const cubey::render::RenderTargetView target = cubey::render::render_target_view(
            frame.color_target, cubey::render::depth_target_view(depth_attachment()));
        const cubey::render::RenderClearValues clear_values{
            .color = color_clear,
            .depth = depth_clear,
        };
        const cubey::render::RenderTargetRenderingInfo rendering(target, clear_values);

        vkCmdBeginRendering(command_buffer, &rendering.info());
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        const VkDescriptorSet descriptor_set = descriptors().set();
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout().handle(), 0, 1, &descriptor_set, 0, nullptr);
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
            cubey::vulkan::finish_color_attachment_for_present_transition(
                frame.color_target.image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer textured_cube");
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

    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& descriptors() const {
        if (!descriptors_.has_value()) {
            throw std::runtime_error("texture descriptors are not initialized");
        }
        return descriptors_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& compute_descriptors() const {
        if (!compute_descriptors_.has_value()) {
            throw std::runtime_error("compute descriptors are not initialized");
        }
        return compute_descriptors_.value();
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
    cubey::OrbitCamera3D camera_;
    OrbitController orbit_controller_;

    std::optional<cubey::vulkan::Buffer> vertex_buffer_;
    std::optional<cubey::vulkan::Buffer> index_buffer_;
    std::optional<cubey::vulkan::Buffer> scene_uniform_buffer_;
    std::optional<cubey::vulkan::Image> texture_image_;
    std::optional<cubey::vulkan::Sampler> texture_sampler_;
    std::optional<cubey::vulkan::DescriptorSetBundle> descriptors_;
    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
    std::optional<cubey::vulkan::DescriptorSetBundle> compute_descriptors_;
    std::optional<cubey::vulkan::PipelineLayout> compute_pipeline_layout_;
    std::optional<cubey::vulkan::ComputePipeline> compute_pipeline_;
};

} // namespace

int run_textured_cube(const RunConfig& config) {
    TexturedCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::textured_cube
