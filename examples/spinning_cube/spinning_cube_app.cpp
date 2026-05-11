#include "spinning_cube_app.h"

#include <cubey/app/windowed_host.h>
#include <cubey/camera_3d.h>
#include <cubey/engine.h>
#include <cubey/math.h>
#include <cubey/render/mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/render/view_3d.h>
#include <cubey/scene.h>
#include <cubey/spirv_io.h>
#include <cubey/transform_3d.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_SPINNING_CUBE_SHADER_DIR) / filename;
}

struct PushConstants {
    cubey::math::Mat4 mvp;
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
                .update =
                    [this](cubey::app::WindowedAppContext& context, const FrameTiming& timing) {
                        (void)context;
                        (void)timing;
                        update_scene_transform();
                    },
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context,
                           const cubey::app::WindowedRenderFrame& frame) {
                        (void)context;
                        record_cube_frame(frame);
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
        if (meshes_.contains(cube_mesh_handle_)) {
            return;
        }
        cube_mesh_handle_ = engine_.render_resources().create_mesh("spinning_cube.cube");
        cube_material_handle_ =
            engine_.render_resources().create_material("spinning_cube.material");
        create_cube_mesh(context);
        create_scene();
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
        destroy_scene_if_needed();
        destroy_render_handles();
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

    void create_cube_mesh(cubey::app::WindowedAppContext& context) {
        meshes_.emplace(cube_mesh_handle_, context.device(),
                        cubey::render::indexed_mesh_config(kCubeVertices, kCubeIndices));
    }

    void create_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        cube_entity_ = setup.entities().create();
        camera_entity_ = setup.entities().create();
        setup.transforms3d().create(cube_entity_, cubey::Transform3D{});
        setup.renderables3d().create(cube_entity_,
                                     cubey::Renderable3D{
                                         .primitives =
                                             {
                                                 cubey::RenderablePrimitive3D{
                                                     .mesh = cube_mesh_handle_,
                                                     .material = cube_material_handle_,
                                                 },
                                             },
                                         .local_bounds =
                                             cubey::Bounds3D{
                                                 .center = {0.0F, 0.0F, 0.0F},
                                                 .half_extent = {1.0F, 1.0F, 1.0F},
                                             },
                                     });
        setup.transforms3d().create(camera_entity_, cubey::orbit_camera_transform(
                                                        cubey::OrbitCameraState{.distance = 4.2F}));
        setup.cameras3d().create(camera_entity_, cubey::Camera3D{});
        setup.commit();
    }

    void update_scene_transform() {
        const auto now = std::chrono::steady_clock::now();
        const float seconds =
            static_cast<float>(std::chrono::duration<double>(now - start_time_).count());

        const cubey::Transform3D transform{
            .rotation = cubey::math::euler_xyz_quat({seconds * 0.55F, seconds * 0.9F, 0.0F}),
        };
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(cube_entity_, transform);
        scene().commit(edits);
    }

    [[nodiscard]] PushConstants
    current_push_constants(const cubey::render::RenderFramePlan3D& plan,
                           const cubey::render::RenderDrawPacket3D& packet) const {
        return {
            plan.view_projection_matrix * packet.world_affine_matrix,
        };
    }

    [[nodiscard]] cubey::render::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const {
        const cubey::render::View3D render_view{
            .camera_entity = camera_entity_,
            .width = extent.width,
            .height = extent.height,
        };
        cubey::render::RenderFramePlan3D plan = cubey::render::build_render_frame_plan_3d(
            render_view, view, engine_.render_resources());
        if (plan.draw_packets.size() != 1) {
            throw std::runtime_error("spinning_cube scene should produce one draw packet");
        }
        return plan;
    }

    void record_cube_frame(const cubey::app::WindowedRenderFrame& frame) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::render::RenderFramePlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        const cubey::render::RenderDrawPacket3D& draw_packet = frame_plan.draw_packets[0];
        const VkCommandBuffer command_buffer = frame.command_buffer;
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_color_attachment_transition(frame.color_target.image));
        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_depth_attachment_transition(depth_attachment().handle()));

        VkClearValue color_clear{};
        color_clear.color = {{0.015F, 0.017F, 0.024F, 1.0F}};
        VkClearValue depth_clear{};
        depth_clear.depthStencil = {1.0F, 0};
        const cubey::render::RenderTargetView target = cubey::render::render_target_view(
            frame.color_target, cubey::render::depth_target_view(depth_attachment()));
        const cubey::render::RenderClearValues clear_values{
            .color = color_clear,
            .depth = depth_clear,
        };
        const cubey::render::RenderTargetRenderingInfo rendering(target, clear_values);

        const PushConstants push_constants = current_push_constants(frame_plan, draw_packet);

        vkCmdBeginRendering(command_buffer, &rendering.info());
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        const cubey::render::DrawItem draw_item{
            .mesh = &mesh(draw_packet.mesh),
            .instance_count = draw_packet.instance_count,
            .first_index = draw_packet.first_index,
            .vertex_offset = draw_packet.vertex_offset,
            .first_instance = draw_packet.first_instance,
        };
        vkCmdPushConstants(command_buffer, pipeline_layout().handle(), VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(PushConstants), &push_constants);
        cubey::render::record_draw_item(command_buffer, draw_item);
        vkCmdEndRendering(command_buffer);

        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::finish_color_attachment_for_present_transition(
                                frame.color_target.image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer spinning_cube");
    }

    [[nodiscard]] const cubey::render::Mesh& mesh(cubey::render::MeshHandle handle) const {
        return meshes_.at(handle);
    }

    [[nodiscard]] cubey::Scene& scene() {
        if (scene_ == nullptr) {
            throw std::runtime_error("spinning_cube scene is not initialized");
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
        camera_entity_ = {};
    }

    void destroy_render_handles() {
        if (meshes_.contains(cube_mesh_handle_)) {
            meshes_.erase(cube_mesh_handle_);
        }
        if (engine_.render_resources().is_alive(cube_mesh_handle_)) {
            engine_.render_resources().destroy_mesh(cube_mesh_handle_);
            cube_mesh_handle_ = {};
        }
        if (engine_.render_resources().is_alive(cube_material_handle_)) {
            engine_.render_resources().destroy_material(cube_material_handle_);
            cube_material_handle_ = {};
        }
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
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity camera_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MaterialHandle cube_material_handle_{};
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
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
