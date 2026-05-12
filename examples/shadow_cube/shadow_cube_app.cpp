#include "shadow_cube_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/windowed_host.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/sampler.h>
#include <cubey/vulkan/shader_bytecode.h>
#include <cubey/vulkan/shader_module.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_SHADOW_CUBE_SHADER_DIR
#error "CUBEY_SHADOW_CUBE_SHADER_DIR must be defined by the shadow_cube CMake target"
#endif

namespace cubey::examples::shadow_cube {
namespace {

constexpr std::uint32_t kShadowMapSize = 1024;
const cubey::math::Vec3 kLightDirection = glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_SHADOW_CUBE_SHADER_DIR) / filename;
}

struct ShadowPushConstants {
    cubey::math::Mat4 light_mvp;
};

struct ScenePushConstants {
    cubey::math::Mat4 mvp;
    cubey::math::Mat4 light_mvp;
};

static_assert(sizeof(ShadowPushConstants) == sizeof(cubey::math::Mat4));
static_assert(sizeof(ScenePushConstants) == sizeof(cubey::math::Mat4) * 2U);

cubey::render::MaterialPassInfo shadow_depth_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ShadowPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "shadow_cube.depth",
        .kind = cubey::render::MaterialPassKind::DepthOnly,
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

cubey::render::MaterialPassInfo shadow_scene_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(ScenePushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "shadow_cube.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState undefined_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState sampled_depth_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState present_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };
}

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
    std::array<float, 3> normal;
};

constexpr std::array<Vertex, 24> kCubeVertices{{
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.88F, 0.35F, 0.26F}, {0.0F, 0.0F, 1.0F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {0.88F, 0.35F, 0.26F}, {0.0F, 0.0F, 1.0F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.88F, 0.35F, 0.26F}, {0.0F, 0.0F, 1.0F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.88F, 0.35F, 0.26F}, {0.0F, 0.0F, 1.0F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.24F, 0.58F, 0.86F}, {0.0F, 0.0F, -1.0F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.24F, 0.58F, 0.86F}, {0.0F, 0.0F, -1.0F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.24F, 0.58F, 0.86F}, {0.0F, 0.0F, -1.0F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.24F, 0.58F, 0.86F}, {0.0F, 0.0F, -1.0F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.32F, 0.68F, 0.38F}, {-1.0F, 0.0F, 0.0F}},
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.32F, 0.68F, 0.38F}, {-1.0F, 0.0F, 0.0F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.32F, 0.68F, 0.38F}, {-1.0F, 0.0F, 0.0F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.32F, 0.68F, 0.38F}, {-1.0F, 0.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {0.92F, 0.70F, 0.22F}, {1.0F, 0.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.92F, 0.70F, 0.22F}, {1.0F, 0.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.92F, 0.70F, 0.22F}, {1.0F, 0.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.92F, 0.70F, 0.22F}, {1.0F, 0.0F, 0.0F}},
    Vertex{{-1.0F, 1.0F, 1.0F}, {0.66F, 0.44F, 0.86F}, {0.0F, 1.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, 1.0F}, {0.66F, 0.44F, 0.86F}, {0.0F, 1.0F, 0.0F}},
    Vertex{{1.0F, 1.0F, -1.0F}, {0.66F, 0.44F, 0.86F}, {0.0F, 1.0F, 0.0F}},
    Vertex{{-1.0F, 1.0F, -1.0F}, {0.66F, 0.44F, 0.86F}, {0.0F, 1.0F, 0.0F}},
    Vertex{{-1.0F, -1.0F, -1.0F}, {0.30F, 0.72F, 0.74F}, {0.0F, -1.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, -1.0F}, {0.30F, 0.72F, 0.74F}, {0.0F, -1.0F, 0.0F}},
    Vertex{{1.0F, -1.0F, 1.0F}, {0.30F, 0.72F, 0.74F}, {0.0F, -1.0F, 0.0F}},
    Vertex{{-1.0F, -1.0F, 1.0F}, {0.30F, 0.72F, 0.74F}, {0.0F, -1.0F, 0.0F}},
}};

constexpr std::array<std::uint16_t, 36> kCubeIndices{{
    0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
}};

constexpr std::array<Vertex, 4> kFloorVertices{{
    Vertex{{-4.0F, -1.05F, -4.0F}, {0.58F, 0.58F, 0.52F}, {0.0F, 1.0F, 0.0F}},
    Vertex{{4.0F, -1.05F, -4.0F}, {0.58F, 0.58F, 0.52F}, {0.0F, 1.0F, 0.0F}},
    Vertex{{4.0F, -1.05F, 4.0F}, {0.58F, 0.58F, 0.52F}, {0.0F, 1.0F, 0.0F}},
    Vertex{{-4.0F, -1.05F, 4.0F}, {0.58F, 0.58F, 0.52F}, {0.0F, 1.0F, 0.0F}},
}};

constexpr std::array<std::uint16_t, 6> kFloorIndices{{0, 1, 2, 0, 2, 3}};

cubey::Transform3D look_at_transform(cubey::math::Vec3 eye, cubey::math::Vec3 target) {
    const cubey::math::Vec3 forward = glm::normalize(target - eye);
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    if (std::abs(glm::dot(forward, up)) > 0.95F) {
        up = {0.0F, 0.0F, 1.0F};
    }
    return {
        .translation = eye,
        .rotation = glm::quatLookAtRH(forward, up),
    };
}

class ShadowCubeApp {
  public:
    explicit ShadowCubeApp(RunConfig config) : config_(std::move(config)) {}

    ShadowCubeApp(const ShadowCubeApp&) = delete;
    ShadowCubeApp& operator=(const ShadowCubeApp&) = delete;

    int run() {
        if (config_.headless) {
            throw std::runtime_error("shadow_cube does not support --headless yet");
        }

        cubey::host::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            {
                .create_swapchain_resources =
                    [this](cubey::host::WindowedAppContext& context) {
                        create_global_resources_if_needed(context);
                        create_swapchain_resources(context);
                    },
                .destroy_swapchain_resources =
                    [this](cubey::host::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
                .on_ready =
                    [](cubey::host::WindowedAppContext& context) {
                        std::printf("shadow_cube: %s rendering directional shadow cube at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update =
                    [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
                        if (context.input().key_pressed(cubey::input::Key::Escape)) {
                            context.window().request_close();
                        }
                        orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
                        update_camera_transform();
                    },
                .record_frame =
                    [this](cubey::host::WindowedAppContext& context,
                           const cubey::host::WindowedRenderFrame& frame) {
                        record_shadow_frame(context, frame);
                    },
                .frame_stats_sample = {},
                .shutdown =
                    [this](cubey::host::WindowedAppContext& context) {
                        (void)context;
                        destroy_all_resources();
                    },
            });
        return host.run();
    }

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
        if (scene_ != nullptr) {
            return;
        }

        cube_mesh_handle_ = engine_.render_resources().create_mesh("shadow_cube.cube");
        floor_mesh_handle_ = engine_.render_resources().create_mesh("shadow_cube.floor");
        material_handle_ = engine_.render_resources().create_material("shadow_cube.material");
        meshes_.emplace(cube_mesh_handle_, context.gpu(),
                        cubey::render::indexed_mesh_config(kCubeVertices, kCubeIndices));
        meshes_.emplace(floor_mesh_handle_, context.gpu(),
                        cubey::render::indexed_mesh_config(kFloorVertices, kFloorIndices));
        create_scene();
        create_shadow_depth_resources(context);
        create_descriptors(context);
    }

    void create_swapchain_resources(cubey::host::WindowedAppContext& context) {
        depth_attachment_.emplace(context.device(), context.swapchain().extent());
        graph_resources_.clear();
        graph_resources_.resize(context.frame_slot_count());
        create_present_resources(context);
        create_pipelines(context);
    }

    void destroy_swapchain_resources() {
        graph_resources_.clear();
        present_pipeline_.reset();
        present_pipeline_layout_.reset();
        present_descriptors_.reset();
        present_sampler_.reset();
        scene_pipeline_.reset();
        scene_pipeline_layout_.reset();
        shadow_pipeline_.reset();
        shadow_pipeline_layout_.reset();
        depth_attachment_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        descriptors_.reset();
        shadow_depth_.reset();
        shadow_depth_is_sampled_ = false;
        destroy_scene_if_needed();
        destroy_render_handles();
    }

    void create_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        cube_entity_ = setup.entities().create();
        floor_entity_ = setup.entities().create();
        camera_entity_ = setup.entities().create();
        light_camera_entity_ = setup.entities().create();
        light_entity_ = setup.entities().create();

        setup.transforms3d().create(cube_entity_, cubey::Transform3D{
                                                      .scale = {0.82F, 0.82F, 0.82F},
                                                  });
        setup.renderables3d().create(cube_entity_, cubey::Renderable3D{
                                                       .primitives =
                                                           {
                                                               cubey::RenderablePrimitive3D{
                                                                   .mesh = cube_mesh_handle_,
                                                                   .material = material_handle_,
                                                               },
                                                           },
                                                       .local_bounds =
                                                           cubey::Bounds3D{
                                                               .center = {0.0F, 0.0F, 0.0F},
                                                               .half_extent = {1.0F, 1.0F, 1.0F},
                                                           },
                                                   });

        setup.transforms3d().create(floor_entity_, cubey::Transform3D{});
        setup.renderables3d().create(floor_entity_, cubey::Renderable3D{
                                                        .primitives =
                                                            {
                                                                cubey::RenderablePrimitive3D{
                                                                    .mesh = floor_mesh_handle_,
                                                                    .material = material_handle_,
                                                                },
                                                            },
                                                        .local_bounds =
                                                            cubey::Bounds3D{
                                                                .center = {0.0F, -1.05F, 0.0F},
                                                                .half_extent = {4.0F, 0.01F, 4.0F},
                                                            },
                                                    });

        setup.transforms3d().create(camera_entity_, cubey::orbit_camera_transform(
                                                        cubey::OrbitCameraState{.distance = 5.2F}));
        setup.cameras3d().create(camera_entity_, cubey::Camera3D{});

        const cubey::math::Vec3 light_eye = kLightDirection * 6.0F;
        setup.transforms3d().create(light_camera_entity_,
                                    look_at_transform(light_eye, {0.0F, 0.0F, 0.0F}));
        setup.cameras3d().create(light_camera_entity_,
                                 cubey::Camera3D({
                                     .projection = cubey::Camera3DProjection::Orthographic,
                                     .orthographic_height = 7.0F,
                                     .near_z = 0.1F,
                                     .far_z = 14.0F,
                                 }));

        cubey::Light3D sunlight =
            cubey::directional_light_3d(kLightDirection, {1.0F, 0.94F, 0.82F}, 1.0F);
        sunlight.casts_shadows = true;
        setup.lights3d().create(light_entity_, sunlight);
        setup.commit();
    }

    void create_shadow_depth_resources(cubey::host::WindowedAppContext& context) {
        const VkFormat shadow_format = cubey::vulkan::choose_depth_format(context.device());
        shadow_depth_.emplace(context.device(),
                              cubey::render::DepthTextureConfig{
                                  .extent = {kShadowMapSize, kShadowMapSize},
                                  .format = shadow_format,
                                  .create_sampler = true,
                                  .sampler =
                                      {
                                          .min_filter = VK_FILTER_NEAREST,
                                          .mag_filter = VK_FILTER_NEAREST,
                                          .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                          .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                                      },
                              });
    }

    void create_descriptors(cubey::host::WindowedAppContext& context) {
        const cubey::render::MaterialPassInfo material_pass = shadow_scene_pass_info();
        cubey::render::validate_material_pass_info(material_pass);
        if (material_pass.descriptor_sets.size() != 1 ||
            material_pass.descriptor_sets[0].set != 0) {
            throw std::runtime_error("shadow_cube scene pass should declare descriptor set 0");
        }
        descriptors_.emplace(context.device(), cubey::vulkan::DescriptorSetInfo(
                                                   material_pass.descriptor_sets[0].bindings));

        const cubey::vulkan::DescriptorImageWrite shadow_write =
            cubey::vulkan::combined_image_sampler_descriptor(
                descriptors().set(), 0, shadow_depth().sampler().handle(), shadow_depth().view(),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        const std::array<VkWriteDescriptorSet, 1> writes{shadow_write.descriptor_write()};
        cubey::vulkan::update_descriptor_sets(context.device(), writes);
    }

    void create_present_resources(cubey::host::WindowedAppContext& context) {
        present_sampler_.emplace(context.device(),
                                 cubey::vulkan::SamplerConfig{
                                     .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                 });
        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{
            cubey::vulkan::DescriptorSetBindingConfig{
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        present_descriptors_.emplace(context.device(), cubey::vulkan::DescriptorSetInfo(
                                                           bindings, context.frame_slot_count()));
    }

    void create_pipelines(cubey::host::WindowedAppContext& context) {
        create_shadow_pipeline(context);
        create_scene_pipeline(context);
        create_present_pipeline(context);
    }

    void create_shadow_pipeline(cubey::host::WindowedAppContext& context) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::vulkan::read_spirv_file(shader_path("shadow_depth.vert.spv"));
        cubey::vulkan::ShaderModule vertex_shader(context.device(), vertex_code);
        const VkPipelineShaderStageCreateInfo vertex_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle());

        VkVertexInputBindingDescription vertex_binding{};
        vertex_binding.binding = 0;
        vertex_binding.stride = sizeof(Vertex);
        vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vertex_attribute{};
        vertex_attribute.location = 0;
        vertex_attribute.binding = 0;
        vertex_attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        vertex_attribute.offset = offsetof(Vertex, position);

        const cubey::render::MaterialPassInfo material_pass = shadow_depth_pass_info();

        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = {},
            .push_constants = {material_pass.push_constants.data(),
                               material_pass.push_constants.size()},
        });
        shadow_pipeline_layout_.emplace(context.device(), layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = shadow_pipeline_layout().handle();
        pipeline_config.extent = shadow_depth().extent();
        pipeline_config.color_format = VK_FORMAT_UNDEFINED;
        pipeline_config.depth_format = shadow_depth().format();
        pipeline_config.shader_stages = {&vertex_stage, 1};
        pipeline_config.vertex_bindings = {&vertex_binding, 1};
        pipeline_config.vertex_attributes = {&vertex_attribute, 1};
        cubey::render::apply_material_pass_state(material_pass, pipeline_config);
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        shadow_pipeline_.emplace(context.device(), pipeline_info.create_info());
    }

    void create_scene_pipeline(cubey::host::WindowedAppContext& context) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::vulkan::read_spirv_file(shader_path("shadow_cube.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::vulkan::read_spirv_file(shader_path("shadow_cube.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(context.device(), vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(context.device(), fragment_code);
        const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle()),
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader.handle()),
        };

        VkVertexInputBindingDescription vertex_binding{};
        vertex_binding.binding = 0;
        vertex_binding.stride = sizeof(Vertex);
        vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> vertex_attributes{};
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

        const cubey::render::MaterialPassInfo material_pass = shadow_scene_pass_info();

        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = {material_pass.push_constants.data(),
                               material_pass.push_constants.size()},
        });
        scene_pipeline_layout_.emplace(context.device(), layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = scene_pipeline_layout().handle();
        pipeline_config.extent = context.swapchain().extent();
        pipeline_config.color_format = context.swapchain().format();
        pipeline_config.depth_format = depth_attachment().format();
        pipeline_config.shader_stages = shader_stages;
        pipeline_config.vertex_bindings = {&vertex_binding, 1};
        pipeline_config.vertex_attributes = vertex_attributes;
        cubey::render::apply_material_pass_state(material_pass, pipeline_config);
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        scene_pipeline_.emplace(context.device(), pipeline_info.create_info());
    }

    void create_present_pipeline(cubey::host::WindowedAppContext& context) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::vulkan::read_spirv_file(shader_path("shadow_present.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::vulkan::read_spirv_file(shader_path("shadow_present.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(context.device(), vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(context.device(), fragment_code);
        const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle()),
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader.handle()),
        };

        const std::array<VkDescriptorSetLayout, 1> set_layouts{present_descriptors().layout()};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = {},
        });
        present_pipeline_layout_.emplace(context.device(), layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = present_pipeline_layout().handle();
        pipeline_config.extent = context.swapchain().extent();
        pipeline_config.color_format = context.swapchain().format();
        pipeline_config.shader_stages = shader_stages;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        present_pipeline_.emplace(context.device(), pipeline_info.create_info());
    }

    void update_camera_transform() {
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(
            camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                .distance = 5.2F,
                                .yaw = orbit_controller_.yaw(),
                                .pitch = orbit_controller_.pitch(),
                            }));
        scene().commit(edits);
    }

    [[nodiscard]] cubey::scene::FrameRenderPlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D color_extent) const {
        const cubey::scene::View3D shadow_view{
            .camera_entity = light_camera_entity_,
            .width = kShadowMapSize,
            .height = kShadowMapSize,
            .culling_enabled = false,
        };
        const cubey::scene::View3D scene_view{
            .camera_entity = camera_entity_,
            .width = color_extent.width,
            .height = color_extent.height,
            .environment =
                cubey::scene::Environment3D{
                    .ambient_color = {0.22F, 0.22F, 0.22F},
                    .ambient_intensity = 1.0F,
                },
        };
        return cubey::scene::FrameRenderPlan3D({
            cubey::scene::RenderPassPlan3D{
                .label = "shadow",
                .kind = cubey::scene::RenderPassKind3D::DepthOnly,
                .frame_plan = cubey::scene::build_render_frame_plan_3d(shadow_view, view,
                                                                       engine_.render_resources()),
            },
            cubey::scene::RenderPassPlan3D{
                .label = "scene",
                .kind = cubey::scene::RenderPassKind3D::Color,
                .frame_plan = cubey::scene::build_render_frame_plan_3d(scene_view, view,
                                                                       engine_.render_resources()),
            },
        });
    }

    struct ShadowRenderGraph {
        cubey::render::CompiledRenderGraph graph;
        cubey::render::RenderGraphTextureHandle scene_color;
    };

    [[nodiscard]] ShadowRenderGraph
    current_render_graph(const cubey::host::WindowedRenderFrame& frame,
                         const cubey::vulkan::CommandRecorder& recorder,
                         const cubey::scene::RenderFramePlan3D& shadow_plan,
                         const cubey::scene::RenderFramePlan3D& scene_plan) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer_handle = graph.import_color_target(
            "backbuffer", frame.color_target, undefined_texture_state(), present_texture_state());
        const cubey::render::RenderGraphTextureHandle scene_color_handle =
            graph.create_texture(cubey::render::RenderGraphTextureDesc{
                .label = "scene color",
                .extent = frame.color_target.extent,
                .format = frame.color_target.format,
                .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
            });
        const cubey::render::RenderGraphTextureHandle scene_depth_handle =
            graph.import_depth_target("scene depth",
                                      cubey::render::depth_target_view(depth_attachment()),
                                      undefined_texture_state());
        const std::optional<cubey::render::RenderGraphTextureState> shadow_initial_state =
            shadow_depth_is_sampled_ ? sampled_depth_texture_state() : undefined_texture_state();
        const cubey::render::RenderGraphTextureHandle shadow_depth_handle =
            graph.import_depth_target("shadow depth",
                                      cubey::render::depth_target_view(shadow_depth()),
                                      shadow_initial_state);

        graph.add_pass("shadow", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_depth(shadow_depth_handle)
            .material_pass(shadow_depth_pass_info())
            .execute([this, &recorder,
                      &shadow_plan](const cubey::render::RenderGraphExecutionContext& context) {
                cubey::render::record_render_graph_barriers(
                    recorder, context, cubey::render::RenderGraphBarrierPhase::BeforePass);
                record_shadow_pass(recorder, shadow_plan);
            });
        graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(shadow_depth_handle)
            .write_color(scene_color_handle)
            .write_depth(scene_depth_handle)
            .material_pass(shadow_scene_pass_info())
            .execute([this, &recorder, scene_color_handle, &scene_plan,
                      &shadow_plan](const cubey::render::RenderGraphExecutionContext& context) {
                cubey::render::record_render_graph_barriers(
                    recorder, context, cubey::render::RenderGraphBarrierPhase::BeforePass);
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, scene_color_handle);
                record_scene_pass(recorder, target, scene_plan, shadow_plan);
            });
        graph.add_pass("present", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(scene_color_handle)
            .write_color(backbuffer_handle)
            .execute([this, &recorder,
                      &frame](const cubey::render::RenderGraphExecutionContext& context) {
                cubey::render::record_render_graph_barriers(
                    recorder, context, cubey::render::RenderGraphBarrierPhase::BeforePass);
                record_present_pass(recorder, frame);
                cubey::render::record_render_graph_barriers(
                    recorder, context, cubey::render::RenderGraphBarrierPhase::AfterPass);
            });

        return {
            .graph = graph.compile(),
            .scene_color = scene_color_handle,
        };
    }

    void update_present_descriptor(cubey::host::WindowedAppContext& context,
                                   std::uint32_t frame_slot_index,
                                   const cubey::render::RenderGraphResourceSet& resources,
                                   cubey::render::RenderGraphTextureHandle scene_color) const {
        const std::optional<cubey::render::RenderGraphResolvedTexture> resolved =
            resources.texture(scene_color);
        if (!resolved.has_value()) {
            throw std::runtime_error("shadow_cube scene color was not allocated");
        }
        const cubey::vulkan::DescriptorImageWrite image_write =
            cubey::vulkan::combined_image_sampler_descriptor(
                present_descriptors().set(frame_slot_index), 0, present_sampler().handle(),
                resolved->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const std::array<VkWriteDescriptorSet, 1> writes{image_write.descriptor_write()};
        cubey::vulkan::update_descriptor_sets(context.device(), writes);
    }

    void record_shadow_frame(cubey::host::WindowedAppContext& context,
                             const cubey::host::WindowedRenderFrame& frame) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::FrameRenderPlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        if (frame_plan.passes().size() != 2) {
            throw std::runtime_error("shadow_cube frame plan should have two passes");
        }
        const cubey::scene::RenderFramePlan3D& shadow_plan = frame_plan.passes()[0].frame_plan;
        const cubey::scene::RenderFramePlan3D& scene_plan = frame_plan.passes()[1].frame_plan;

        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        const ShadowRenderGraph render_graph =
            current_render_graph(frame, recorder, shadow_plan, scene_plan);
        if (frame.frame_slot.index >= graph_resources_.size()) {
            throw std::runtime_error("shadow_cube frame slot is out of range");
        }
        std::optional<cubey::render::RenderGraphResourceSet>& resources =
            graph_resources_.at(frame.frame_slot.index);
        resources.emplace(context.device(), render_graph.graph);
        update_present_descriptor(context, frame.frame_slot.index, *resources,
                                  render_graph.scene_color);

        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        render_graph.graph.execute(*resources);
        recorder.end("vkEndCommandBuffer shadow_cube");
        shadow_depth_is_sampled_ = true;
    }

    void record_shadow_pass(const cubey::vulkan::CommandRecorder& recorder,
                            const cubey::scene::RenderFramePlan3D& shadow_plan) const {
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};
        const cubey::render::DepthOnlyRenderingInfo rendering(
            cubey::render::depth_target_view(shadow_depth()), depth_clear);

        recorder.begin_rendering(rendering.info());
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline().handle());
        for (const cubey::scene::RenderDrawPacket3D& packet : shadow_plan.draw_packets) {
            if (!packet.cast_shadows ||
                !cubey::render::material_supports_pass(
                    packet.material_info, cubey::render::MaterialPassKind::DepthOnly)) {
                continue;
            }
            const ShadowPushConstants push_constants{
                .light_mvp = shadow_plan.view_projection_matrix * packet.world_affine_matrix,
            };
            recorder.push_constants(shadow_pipeline_layout().handle(), VK_SHADER_STAGE_VERTEX_BIT,
                                    0, push_constants);
            const cubey::render::RenderItem render_item =
                cubey::scene::render_item_from_packet(packet);
            const cubey::render::DrawItem draw_item =
                cubey::render::resolve_draw_item(render_item, meshes_);
            cubey::render::record_draw_item(recorder.handle(), draw_item);
        }
        recorder.end_rendering();
    }

    void record_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                           cubey::render::ColorTargetView color_target,
                           const cubey::scene::RenderFramePlan3D& scene_plan,
                           const cubey::scene::RenderFramePlan3D& shadow_plan) const {
        VkClearValue color_clear{};
        color_clear.color = {{0.026F, 0.029F, 0.034F, 1.0F}};
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};
        const cubey::render::RenderTargetRenderingInfo rendering(
            cubey::render::render_target_view(color_target,
                                              cubey::render::depth_target_view(depth_attachment())),
            cubey::render::RenderClearValues{
                .color = color_clear,
                .depth = depth_clear,
            });

        recorder.begin_rendering(rendering.info());
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, scene_pipeline().handle());
        const VkDescriptorSet descriptor_set = descriptors().set();
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     scene_pipeline_layout().handle(), 0, descriptor_set);
        for (const cubey::scene::RenderDrawPacket3D& packet : scene_plan.draw_packets) {
            if (!cubey::render::material_supports_pass(
                    packet.material_info, cubey::render::MaterialPassKind::ForwardColor)) {
                continue;
            }
            const ScenePushConstants push_constants{
                .mvp = scene_plan.view_projection_matrix * packet.world_affine_matrix,
                .light_mvp = shadow_plan.view_projection_matrix * packet.world_affine_matrix,
            };
            recorder.push_constants(scene_pipeline_layout().handle(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    push_constants);
            const cubey::render::RenderItem render_item =
                cubey::scene::render_item_from_packet(packet);
            const cubey::render::DrawItem draw_item =
                cubey::render::resolve_draw_item(render_item, meshes_);
            cubey::render::record_draw_item(recorder.handle(), draw_item);
        }
        recorder.end_rendering();
    }

    void record_present_pass(const cubey::vulkan::CommandRecorder& recorder,
                             const cubey::host::WindowedRenderFrame& frame) const {
        VkClearValue color_clear{};
        color_clear.color = {{0.0F, 0.0F, 0.0F, 1.0F}};
        const cubey::render::RenderTargetRenderingInfo rendering(
            cubey::render::render_target_view(frame.color_target), cubey::render::RenderClearValues{
                                                                       .color = color_clear,
                                                                   });

        recorder.begin_rendering(rendering.info());
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, present_pipeline().handle());
        const VkDescriptorSet descriptor_set = present_descriptors().set(frame.frame_slot.index);
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     present_pipeline_layout().handle(), 0, descriptor_set);
        recorder.draw(3);
        recorder.end_rendering();
    }

    [[nodiscard]] cubey::Scene& scene() {
        if (scene_ == nullptr) {
            throw std::runtime_error("shadow_cube scene is not initialized");
        }
        return *scene_;
    }

    void destroy_scene_if_needed() {
        if (scene_ == nullptr) {
            return;
        }
        engine_.destroy_scene(*scene_);
        scene_ = nullptr;
        cube_entity_ = {};
        floor_entity_ = {};
        camera_entity_ = {};
        light_camera_entity_ = {};
        light_entity_ = {};
    }

    void destroy_render_handles() {
        if (meshes_.contains(cube_mesh_handle_)) {
            meshes_.erase(cube_mesh_handle_);
        }
        if (meshes_.contains(floor_mesh_handle_)) {
            meshes_.erase(floor_mesh_handle_);
        }
        if (engine_.render_resources().is_alive(cube_mesh_handle_)) {
            engine_.render_resources().destroy_mesh(cube_mesh_handle_);
            cube_mesh_handle_ = {};
        }
        if (engine_.render_resources().is_alive(floor_mesh_handle_)) {
            engine_.render_resources().destroy_mesh(floor_mesh_handle_);
            floor_mesh_handle_ = {};
        }
        if (engine_.render_resources().is_alive(material_handle_)) {
            engine_.render_resources().destroy_material(material_handle_);
            material_handle_ = {};
        }
    }

    [[nodiscard]] const cubey::render::DepthTexture& shadow_depth() const {
        if (!shadow_depth_.has_value()) {
            throw std::runtime_error("shadow depth texture is not initialized");
        }
        return shadow_depth_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& descriptors() const {
        if (!descriptors_.has_value()) {
            throw std::runtime_error("shadow descriptors are not initialized");
        }
        return descriptors_.value();
    }

    [[nodiscard]] const cubey::vulkan::Sampler& present_sampler() const {
        if (!present_sampler_.has_value()) {
            throw std::runtime_error("present sampler is not initialized");
        }
        return present_sampler_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetArray& present_descriptors() const {
        if (!present_descriptors_.has_value()) {
            throw std::runtime_error("present descriptors are not initialized");
        }
        return present_descriptors_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& shadow_pipeline_layout() const {
        if (!shadow_pipeline_layout_.has_value()) {
            throw std::runtime_error("shadow pipeline layout is not initialized");
        }
        return shadow_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::GraphicsPipeline& shadow_pipeline() const {
        if (!shadow_pipeline_.has_value()) {
            throw std::runtime_error("shadow pipeline is not initialized");
        }
        return shadow_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& scene_pipeline_layout() const {
        if (!scene_pipeline_layout_.has_value()) {
            throw std::runtime_error("scene pipeline layout is not initialized");
        }
        return scene_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::GraphicsPipeline& scene_pipeline() const {
        if (!scene_pipeline_.has_value()) {
            throw std::runtime_error("scene pipeline is not initialized");
        }
        return scene_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& present_pipeline_layout() const {
        if (!present_pipeline_layout_.has_value()) {
            throw std::runtime_error("present pipeline layout is not initialized");
        }
        return present_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::GraphicsPipeline& present_pipeline() const {
        if (!present_pipeline_.has_value()) {
            throw std::runtime_error("present pipeline is not initialized");
        }
        return present_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const {
        if (!depth_attachment_.has_value()) {
            throw std::runtime_error("depth attachment is not initialized");
        }
        return depth_attachment_.value();
    }

    RunConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity floor_entity_;
    cubey::Entity camera_entity_;
    cubey::Entity light_camera_entity_;
    cubey::Entity light_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MeshHandle floor_mesh_handle_{};
    cubey::render::MaterialHandle material_handle_{};
    cubey::OrbitController orbit_controller_;
    bool shadow_depth_is_sampled_ = false;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::vector<std::optional<cubey::render::RenderGraphResourceSet>> graph_resources_;
    std::optional<cubey::render::DepthTexture> shadow_depth_;
    std::optional<cubey::vulkan::DescriptorSetBundle> descriptors_;
    std::optional<cubey::vulkan::Sampler> present_sampler_;
    std::optional<cubey::vulkan::DescriptorSetArray> present_descriptors_;
    std::optional<cubey::vulkan::PipelineLayout> shadow_pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> shadow_pipeline_;
    std::optional<cubey::vulkan::PipelineLayout> scene_pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> scene_pipeline_;
    std::optional<cubey::vulkan::PipelineLayout> present_pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> present_pipeline_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
};

} // namespace

int run_shadow_cube(const RunConfig& config) {
    ShadowCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::shadow_cube
