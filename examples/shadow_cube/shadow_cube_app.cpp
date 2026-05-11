#include "shadow_cube_app.h"

#include <cubey/app/windowed_host.h>
#include <cubey/camera_3d.h>
#include <cubey/engine.h>
#include <cubey/math.h>
#include <cubey/orbit_controller.h>
#include <cubey/render/mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/render/view_3d.h>
#include <cubey/scene.h>
#include <cubey/spirv_io.h>
#include <cubey/transform_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/pipeline.h>
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
                        std::printf("shadow_cube: %s rendering directional shadow cube at %ux%u\n",
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
                        update_camera_transform();
                    },
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context,
                           const cubey::app::WindowedRenderFrame& frame) {
                        (void)context;
                        record_shadow_frame(frame);
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

    void create_swapchain_resources(cubey::app::WindowedAppContext& context) {
        depth_attachment_.emplace(context.device(), context.swapchain().extent());
        create_pipelines(context);
    }

    void destroy_swapchain_resources() {
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

    void create_shadow_depth_resources(cubey::app::WindowedAppContext& context) {
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

    void create_descriptors(cubey::app::WindowedAppContext& context) {
        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        }};
        descriptors_.emplace(context.device(), cubey::vulkan::DescriptorSetInfo(bindings));

        const cubey::vulkan::DescriptorImageWrite shadow_write =
            cubey::vulkan::combined_image_sampler_descriptor(
                descriptors().set(), 0, shadow_depth().sampler().handle(), shadow_depth().view(),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        const std::array<VkWriteDescriptorSet, 1> writes{shadow_write.descriptor_write()};
        cubey::vulkan::update_descriptor_sets(context.device(), writes);
    }

    void create_pipelines(cubey::app::WindowedAppContext& context) {
        create_shadow_pipeline(context);
        create_scene_pipeline(context);
    }

    void create_shadow_pipeline(cubey::app::WindowedAppContext& context) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("shadow_depth.vert.spv"));
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

        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(ShadowPushConstants);

        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = {},
            .push_constants = {&push_constant_range, 1},
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
        pipeline_config.depth_test = true;
        pipeline_config.depth_write = true;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        shadow_pipeline_.emplace(context.device(), pipeline_info.create_info());
    }

    void create_scene_pipeline(cubey::app::WindowedAppContext& context) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("shadow_cube.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::read_spirv_file(shader_path("shadow_cube.frag.spv"));
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

        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(ScenePushConstants);

        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = {&push_constant_range, 1},
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
        pipeline_config.depth_test = true;
        pipeline_config.depth_write = true;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        scene_pipeline_.emplace(context.device(), pipeline_info.create_info());
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

    [[nodiscard]] cubey::render::FrameRenderPlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D color_extent) const {
        const cubey::render::View3D shadow_view{
            .camera_entity = light_camera_entity_,
            .width = kShadowMapSize,
            .height = kShadowMapSize,
            .culling_enabled = false,
        };
        const cubey::render::View3D scene_view{
            .camera_entity = camera_entity_,
            .width = color_extent.width,
            .height = color_extent.height,
            .environment =
                cubey::render::Environment3D{
                    .ambient_color = {0.22F, 0.22F, 0.22F},
                    .ambient_intensity = 1.0F,
                },
        };
        return cubey::render::FrameRenderPlan3D({
            cubey::render::RenderPassPlan3D{
                .label = "shadow",
                .kind = cubey::render::RenderPassKind3D::DepthOnly,
                .frame_plan = cubey::render::build_render_frame_plan_3d(shadow_view, view,
                                                                        engine_.render_resources()),
            },
            cubey::render::RenderPassPlan3D{
                .label = "scene",
                .kind = cubey::render::RenderPassKind3D::Color,
                .frame_plan = cubey::render::build_render_frame_plan_3d(scene_view, view,
                                                                        engine_.render_resources()),
            },
        });
    }

    void record_shadow_frame(const cubey::app::WindowedRenderFrame& frame) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::render::FrameRenderPlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        if (frame_plan.passes().size() != 2) {
            throw std::runtime_error("shadow_cube frame plan should have two passes");
        }
        const cubey::render::RenderFramePlan3D& shadow_plan = frame_plan.passes()[0].frame_plan;
        const cubey::render::RenderFramePlan3D& scene_plan = frame_plan.passes()[1].frame_plan;

        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        recorder.transition_image_layout(
            shadow_depth_is_sampled_
                ? cubey::vulkan::begin_sampled_depth_attachment_transition(shadow_depth().handle())
                : cubey::vulkan::begin_depth_attachment_transition(shadow_depth().handle()));
        record_shadow_pass(recorder, shadow_plan);
        recorder.transition_image_layout(
            cubey::vulkan::finish_depth_attachment_for_sampling_transition(
                shadow_depth().handle()));

        recorder.transition_image_layout(
            cubey::vulkan::begin_color_attachment_transition(frame.color_target.image));
        recorder.transition_image_layout(
            cubey::vulkan::begin_depth_attachment_transition(depth_attachment().handle()));
        record_scene_pass(recorder, frame, scene_plan, shadow_plan);
        recorder.transition_image_layout(
            cubey::vulkan::finish_color_attachment_for_present_transition(
                frame.color_target.image));

        recorder.end("vkEndCommandBuffer shadow_cube");
        shadow_depth_is_sampled_ = true;
    }

    void record_shadow_pass(const cubey::vulkan::CommandRecorder& recorder,
                            const cubey::render::RenderFramePlan3D& shadow_plan) const {
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};
        const cubey::render::DepthOnlyRenderingInfo rendering(
            cubey::render::depth_target_view(shadow_depth()), depth_clear);

        recorder.begin_rendering(rendering.info());
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline().handle());
        for (const cubey::render::RenderDrawPacket3D& packet : shadow_plan.draw_packets) {
            const ShadowPushConstants push_constants{
                .light_mvp = shadow_plan.view_projection_matrix * packet.world_affine_matrix,
            };
            recorder.push_constants(shadow_pipeline_layout().handle(), VK_SHADER_STAGE_VERTEX_BIT,
                                    0, push_constants);
            cubey::render::record_draw_item(recorder.handle(), draw_item(packet));
        }
        recorder.end_rendering();
    }

    void record_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                           const cubey::app::WindowedRenderFrame& frame,
                           const cubey::render::RenderFramePlan3D& scene_plan,
                           const cubey::render::RenderFramePlan3D& shadow_plan) const {
        VkClearValue color_clear{};
        color_clear.color = {{0.026F, 0.029F, 0.034F, 1.0F}};
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};
        const cubey::render::RenderTargetRenderingInfo rendering(
            cubey::render::render_target_view(frame.color_target,
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
        for (const cubey::render::RenderDrawPacket3D& packet : scene_plan.draw_packets) {
            const ScenePushConstants push_constants{
                .mvp = scene_plan.view_projection_matrix * packet.world_affine_matrix,
                .light_mvp = shadow_plan.view_projection_matrix * packet.world_affine_matrix,
            };
            recorder.push_constants(scene_pipeline_layout().handle(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    push_constants);
            cubey::render::record_draw_item(recorder.handle(), draw_item(packet));
        }
        recorder.end_rendering();
    }

    [[nodiscard]] cubey::render::DrawItem
    draw_item(const cubey::render::RenderDrawPacket3D& packet) const {
        return {
            .mesh = &mesh(packet.mesh),
            .instance_count = packet.instance_count,
            .first_index = packet.first_index,
            .vertex_offset = packet.vertex_offset,
            .first_instance = packet.first_instance,
        };
    }

    [[nodiscard]] const cubey::render::Mesh& mesh(cubey::render::MeshHandle handle) const {
        return meshes_.at(handle);
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
    std::optional<cubey::render::DepthTexture> shadow_depth_;
    std::optional<cubey::vulkan::DescriptorSetBundle> descriptors_;
    std::optional<cubey::vulkan::PipelineLayout> shadow_pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> shadow_pipeline_;
    std::optional<cubey::vulkan::PipelineLayout> scene_pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> scene_pipeline_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
};

} // namespace

int run_shadow_cube(const RunConfig& config) {
    ShadowCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::shadow_cube
