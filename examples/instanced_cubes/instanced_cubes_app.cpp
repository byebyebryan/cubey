#include "instanced_cubes_app.h"

#include "../common/cube_scene.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/color_space.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/instance_buffer.h>
#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/scene/render_recording.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef CUBEY_INSTANCED_CUBES_SHADER_DIR
#error "CUBEY_INSTANCED_CUBES_SHADER_DIR must be defined by the instanced_cubes CMake target"
#endif

namespace cubey::examples::instanced_cubes {
namespace {

using cubey::host::FrameStatsSample;

constexpr std::uint32_t kGridColumns = 9;
constexpr std::uint32_t kGridRows = 5;
constexpr std::uint32_t kInstanceCount = kGridColumns * kGridRows;
constexpr std::uint32_t kCubeTriangleCount = 12;
constexpr float kGridSpacing = 1.28F;
constexpr float kCameraDistance = 10.2F;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_INSTANCED_CUBES_SHADER_DIR) / filename;
}

struct PushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Mat4 cube_spin;
};

struct CubeInstanceData {
    cubey::math::Mat4 model;
    cubey::math::Vec4 color;
};

static_assert(std::is_trivially_copyable_v<CubeInstanceData>);
static_assert(sizeof(CubeInstanceData) == sizeof(cubey::math::Mat4) + sizeof(cubey::math::Vec4));

cubey::render::MaterialPassInfo instanced_cubes_forward_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "instanced_cubes.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .push_constants = {push_constant_range},
        .depth_test = true,
        .depth_write = true,
    };
}

cubey::render::VertexInputLayout instanced_cube_vertex_input_layout() {
    cubey::render::VertexInputLayout layout =
        cubey::render::vertex_position_color_normal_input_layout();
    layout.vertex_bindings.push_back(cubey::render::instance_input_binding<CubeInstanceData>(1));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CubeInstanceData, model)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
        offsetof(CubeInstanceData, model) + sizeof(cubey::math::Vec4)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        5, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
        offsetof(CubeInstanceData, model) + (sizeof(cubey::math::Vec4) * 2U)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        6, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
        offsetof(CubeInstanceData, model) + (sizeof(cubey::math::Vec4) * 3U)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(CubeInstanceData, color)));
    return layout;
}

std::vector<CubeInstanceData> make_cube_instances() {
    std::vector<CubeInstanceData> instances;
    instances.reserve(kInstanceCount);
    for (std::uint32_t row = 0; row < kGridRows; ++row) {
        for (std::uint32_t column = 0; column < kGridColumns; ++column) {
            const float x = (static_cast<float>(column) - 4.0F) * kGridSpacing;
            const float y = (2.0F - static_cast<float>(row)) * kGridSpacing;
            const float phase = static_cast<float>((row * kGridColumns) + column) * 0.19F;
            const cubey::math::Mat4 model = cubey::math::translation(x, y, 0.0F) *
                                            cubey::math::rotation_y(phase) *
                                            cubey::math::scale({0.42F, 0.42F, 0.42F});
            const float mix_x = static_cast<float>(column) / static_cast<float>(kGridColumns - 1U);
            const float mix_z = static_cast<float>(row) / static_cast<float>(kGridRows - 1U);
            const cubey::math::Vec4 color = cubey::render::srgb_to_linear_rgba({
                0.45F + (0.45F * mix_x),
                0.72F - (0.26F * mix_z),
                0.62F + (0.28F * mix_z),
                1.0F,
            });
            instances.push_back({
                .model = model,
                .color = color,
            });
        }
    }
    return instances;
}

class InstancedCubesApp {
  public:
    explicit InstancedCubesApp(RunConfig config) : config_(std::move(config)) {}

    InstancedCubesApp(const InstancedCubesApp&) = delete;
    InstancedCubesApp& operator=(const InstancedCubesApp&) = delete;

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
            orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
            update_camera_transform();
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            (void)context;
            record_cube_frame(frame);
        };
        callbacks.frame_stats_sample =
            [](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            const VkExtent2D extent = context.swapchain().extent();
            return FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = extent.width,
                .height = extent.height,
                .triangles = kCubeTriangleCount * kInstanceCount,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "instanced_cubes",
                .ready_status = "rendering instanced cube grid",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
        if (meshes_.contains(cube_mesh_handle_)) {
            return;
        }
        cube_material_handle_ =
            engine_.render_resources().create_material("instanced_cubes.material");
        cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
            engine_.render_resources(), meshes_, context.gpu(), "instanced_cubes.cube",
            cubey::render::make_cube_position_color_normal_mesh());
        const std::vector<CubeInstanceData> instances = make_cube_instances();
        instance_buffer_.emplace(context.gpu(), std::span<const CubeInstanceData>(instances));
        create_scene();
    }

    void create_swapchain_resources(cubey::host::WindowedAppContext& context) {
        create_forward_pass(context);
    }

    void destroy_swapchain_resources() {
        forward_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        destroy_scene_if_needed();
        destroy_render_handles();
        instance_buffer_.reset();
    }

    void create_forward_pass(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("instanced_cubes.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("instanced_cubes.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input = instanced_cube_vertex_input_layout();
        forward_pass_.emplace(
            context.device(),
            cubey::render::GraphicsPipelineTargetInfo{
                .extent = context.swapchain().extent(),
                .color_format = context.swapchain().format(),
            },
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .material_pass = instanced_cubes_forward_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.018F, 0.02F, 0.026F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });
    }

    void create_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        const cubey::examples::common::CubeScene3D cube_scene =
            cubey::examples::common::create_cube_scene_3d(
                setup, {
                           .mesh = cube_mesh_handle_,
                           .material = cube_material_handle_,
                           .cube_bounds =
                               cubey::Bounds3D{
                                   .center = {0.0F, 0.0F, 0.0F},
                                   .half_extent = {5.6F, 3.0F, 0.6F},
                               },
                           .camera_distance = kCameraDistance,
                           .instance_count = instance_buffer().count(),
                       });
        cube_entity_ = cube_scene.cube;
        camera_entity_ = cube_scene.camera;
        setup.commit();
    }

    void update_camera_transform() {
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(
            camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                .distance = kCameraDistance,
                                .yaw = orbit_controller_.yaw(),
                                .pitch = orbit_controller_.pitch(),
                            }));
        scene().commit(edits);
    }

    [[nodiscard]] cubey::scene::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const {
        const cubey::scene::View3D render_view{
            .camera_entity = camera_entity_,
            .width = extent.width,
            .height = extent.height,
            .environment =
                cubey::scene::Environment3D{
                    .ambient_color = {0.045F, 0.045F, 0.045F},
                    .ambient_intensity = 1.0F,
                },
        };
        cubey::scene::RenderFramePlan3D plan =
            cubey::scene::build_render_frame_plan_3d(render_view, view, engine_.render_resources());
        if (plan.draw_packets.size() != 1) {
            throw std::runtime_error("instanced_cubes scene should produce one draw packet");
        }
        return plan;
    }

    [[nodiscard]] cubey::math::Mat4 cube_spin_matrix(const FrameTiming& timing) const {
        const float seconds = static_cast<float>(timing.elapsed_seconds);
        return cubey::examples::common::cube_spin_transform(seconds).affine_matrix();
    }

    void record_cube_frame(const cubey::host::WindowedRenderFrame& frame) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::RenderFramePlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        const cubey::math::Mat4 cube_spin = cube_spin_matrix(frame.timing);
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        forward_pass().record_to_present(
            recorder, frame.color_target,
            [this, &frame_plan, &cube_spin](
                const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::scene::record_pipeline_draw_packets_3d(
                    pass_recorder, frame_plan.draw_packets, meshes_,
                    {
                        .pipeline = &forward_pass().pipeline(),
                    },
                    [this, &frame_plan, &cube_spin](
                        const cubey::vulkan::CommandRecorder& packet_recorder,
                        const cubey::scene::RenderDrawPacket3D&) {
                        instance_buffer().bind(packet_recorder, 1);
                        packet_recorder.push_constants(
                            forward_pass().pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                            PushConstants{
                                .view_projection = frame_plan.view_projection_matrix,
                                .cube_spin = cube_spin,
                            });
                    });
            });

        recorder.end("vkEndCommandBuffer instanced_cubes");
    }

    [[nodiscard]] cubey::Scene& scene() {
        if (scene_ == nullptr) {
            throw std::runtime_error("instanced_cubes scene is not initialized");
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
        cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_,
                                             cube_mesh_handle_);
        if (engine_.render_resources().is_alive(cube_material_handle_)) {
            engine_.render_resources().destroy_material(cube_material_handle_);
            cube_material_handle_ = {};
        }
    }

    [[nodiscard]] const cubey::render::InstanceBuffer<CubeInstanceData>& instance_buffer() const {
        if (!instance_buffer_.has_value()) {
            throw std::runtime_error("instance buffer is not initialized");
        }
        return instance_buffer_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity camera_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MaterialHandle cube_material_handle_{};
    OrbitController orbit_controller_;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::optional<cubey::render::InstanceBuffer<CubeInstanceData>> instance_buffer_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace

int run_instanced_cubes(const RunConfig& config) {
    InstancedCubesApp app(config);
    return app.run();
}

} // namespace cubey::examples::instanced_cubes
