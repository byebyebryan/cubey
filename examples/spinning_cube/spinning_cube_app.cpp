#include "spinning_cube_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/windowed_app.h>
#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/render_recording.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image.h>

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_SPINNING_CUBE_SHADER_DIR) / filename;
}

struct PushConstants {
    cubey::math::Mat4 mvp;
};

cubey::render::MaterialPassInfo spinning_cube_forward_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "spinning_cube.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

class SpinningCubeApp {
  public:
    explicit SpinningCubeApp(RunConfig config) : config_(std::move(config)) {}

    SpinningCubeApp(const SpinningCubeApp&) = delete;
    SpinningCubeApp& operator=(const SpinningCubeApp&) = delete;

    int run() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context);
            create_swapchain_resources(context);
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            (void)context;
            (void)timing;
            update_scene_transform();
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            (void)context;
            record_cube_frame(frame);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "spinning_cube",
                .ready_status = "rendering indexed cube",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            std::move(callbacks));
    }

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
        if (meshes_.contains(cube_mesh_handle_)) {
            return;
        }
        cube_mesh_handle_ = engine_.render_resources().create_mesh("spinning_cube.cube");
        cube_material_handle_ =
            engine_.render_resources().create_material("spinning_cube.material");
        create_cube_mesh(context);
        create_scene();
    }

    void create_swapchain_resources(cubey::host::WindowedAppContext& context) {
        create_depth_resources(context);
        create_pipeline(context);
    }

    void destroy_swapchain_resources() {
        pipeline_resource_.reset();
        depth_attachment_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        destroy_scene_if_needed();
        destroy_render_handles();
    }

    void create_pipeline(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("spinning_cube.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("spinning_cube.frag.spv"),
            },
        };

        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_input_layout();

        const cubey::render::MaterialPassInfo material_pass = spinning_cube_forward_pass_info();
        pipeline_resource_.emplace(context.device(),
                                   cubey::render::GraphicsPipelineFileResourceConfig{
                                       .extent = context.swapchain().extent(),
                                       .color_format = context.swapchain().format(),
                                       .depth_format = depth_attachment().format(),
                                       .shader_stage_files = shader_stage_files,
                                       .vertex_bindings = vertex_input.bindings(),
                                       .vertex_attributes = vertex_input.attribute_descriptions(),
                                       .material_pass = material_pass,
                                   });
    }

    void create_depth_resources(cubey::host::WindowedAppContext& context) {
        depth_attachment_.emplace(context.device(), context.swapchain().extent());
    }

    void create_cube_mesh(cubey::host::WindowedAppContext& context) {
        const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColor> cube =
            cubey::render::make_cube_position_color_mesh();
        meshes_.emplace(cube_mesh_handle_, context.gpu(), cube.mesh_config());
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
    current_push_constants(const cubey::scene::RenderFramePlan3D& plan,
                           const cubey::scene::RenderDrawPacket3D& packet) const {
        return {
            plan.view_projection_matrix * packet.world_affine_matrix,
        };
    }

    [[nodiscard]] cubey::scene::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const {
        const cubey::scene::View3D render_view{
            .camera_entity = camera_entity_,
            .width = extent.width,
            .height = extent.height,
        };
        cubey::scene::RenderFramePlan3D plan =
            cubey::scene::build_render_frame_plan_3d(render_view, view, engine_.render_resources());
        if (plan.draw_packets.size() != 1) {
            throw std::runtime_error("spinning_cube scene should produce one draw packet");
        }
        return plan;
    }

    void record_cube_frame(const cubey::host::WindowedRenderFrame& frame) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::RenderFramePlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        const cubey::render::RenderTargetView target = cubey::render::render_target_view(
            frame.color_target, cubey::render::depth_target_view(depth_attachment()));
        const cubey::render::RenderClearValues clear_values{
            .color = cubey::render::color_clear_value(0.015F, 0.017F, 0.024F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        };

        cubey::render::record_present_render_target_pass(
            recorder, target, clear_values,
            [this, &frame_plan](const cubey::vulkan::CommandRecorder& pass_recorder) {
                pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipeline_resource().pipeline());
                cubey::scene::record_draw_packets_3d(
                    pass_recorder, frame_plan.draw_packets, meshes_, {},
                    [this, &frame_plan](const cubey::vulkan::CommandRecorder& packet_recorder,
                                        const cubey::scene::RenderDrawPacket3D& packet) {
                        const PushConstants push_constants =
                            current_push_constants(frame_plan, packet);
                        packet_recorder.push_constants(pipeline_resource().layout(),
                                                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                                                       push_constants);
                    });
            });

        recorder.end("vkEndCommandBuffer spinning_cube");
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

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline_resource() const {
        if (!pipeline_resource_.has_value()) {
            throw std::runtime_error("pipeline resource is not initialized");
        }
        return pipeline_resource_.value();
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
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_resource_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
};

} // namespace

int run_spinning_cube(const RunConfig& config) {
    SpinningCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::spinning_cube
