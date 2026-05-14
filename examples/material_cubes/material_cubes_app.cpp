#include "material_cubes_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/color_space.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/scene/render_recording.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef CUBEY_MATERIAL_CUBES_SHADER_DIR
#error "CUBEY_MATERIAL_CUBES_SHADER_DIR must be defined by the material_cubes CMake target"
#endif

namespace cubey::examples::material_cubes {
namespace {

using cubey::host::FrameStatsSample;

constexpr std::uint32_t kMaterialCubeCount = 9;
constexpr std::uint32_t kCubeTriangleCount = 12;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_MATERIAL_CUBES_SHADER_DIR) / filename;
}

struct PushConstants {
    cubey::math::Mat4 mvp;
    cubey::math::Mat4 model;
};

struct MaterialUniforms {
    cubey::math::Vec4 base_color;
};

static_assert(std::is_trivially_copyable_v<MaterialUniforms>);
static_assert(sizeof(MaterialUniforms) == sizeof(float) * 4U);

struct MaterialCube {
    cubey::Entity entity{};
    cubey::render::MaterialHandle material{};
    cubey::math::Vec4 color{1.0F};
};

cubey::render::MaterialPassInfo material_cubes_forward_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "material_cubes.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
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

constexpr std::array<cubey::math::Vec4, kMaterialCubeCount> kMaterialColors{
    cubey::math::Vec4{0.95F, 0.28F, 0.20F, 1.0F}, cubey::math::Vec4{0.96F, 0.64F, 0.22F, 1.0F},
    cubey::math::Vec4{0.96F, 0.86F, 0.28F, 1.0F}, cubey::math::Vec4{0.34F, 0.78F, 0.42F, 1.0F},
    cubey::math::Vec4{0.28F, 0.74F, 0.86F, 1.0F}, cubey::math::Vec4{0.24F, 0.48F, 0.92F, 1.0F},
    cubey::math::Vec4{0.55F, 0.38F, 0.92F, 1.0F}, cubey::math::Vec4{0.88F, 0.44F, 0.78F, 1.0F},
    cubey::math::Vec4{0.86F, 0.88F, 0.92F, 1.0F},
};

class MaterialCubesApp {
  public:
    explicit MaterialCubesApp(RunConfig config) : config_(std::move(config)) {}

    MaterialCubesApp(const MaterialCubesApp&) = delete;
    MaterialCubesApp& operator=(const MaterialCubesApp&) = delete;

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
        callbacks.on_ready = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            orbit_controller_.set_auto_rotation_speed(0.28F);
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
                .triangles = kCubeTriangleCount * kMaterialCubeCount,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "material_cubes",
                .ready_status = "rendering material instance cubes",
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
        cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
            engine_.render_resources(), meshes_, context.gpu(), "material_cubes.cube",
            cubey::render::make_cube_position_color_normal_mesh());
        create_materials(context);
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
        destroy_material_resources();
        destroy_render_handles();
    }

    void create_materials(cubey::host::WindowedAppContext& context) {
        material_handles_.reserve(kMaterialCubeCount);
        for (std::uint32_t index = 0; index < kMaterialCubeCount; ++index) {
            const cubey::render::MaterialHandle material =
                engine_.render_resources().create_material(cubey::render::MaterialInfo{
                    .label = "material_cubes.material." + std::to_string(index),
                    .sort_key = index,
                });
            material_handles_.push_back(material);
            material_instances_.emplace(material, context.device(),
                                        cubey::render::FrameUniformMaterialInstanceConfig{
                                            .material_pass = material_cubes_forward_pass_info(),
                                            .descriptor_set = 0,
                                            .frame_slot_count = context.frame_slot_count(),
                                            .uniform_binding = 0,
                                        });
        }
    }

    void create_forward_pass(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("material_cubes.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("material_cubes.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_input_layout();
        const std::array<VkDescriptorSetLayout, 1> set_layouts{
            material_instances_.at(material_handles_.front()).layout()};
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
                        .descriptor_set_layouts = set_layouts,
                        .material_pass = material_cubes_forward_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.018F, 0.019F, 0.025F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });
    }

    void create_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        for (std::uint32_t index = 0; index < kMaterialCubeCount; ++index) {
            const float x = (static_cast<float>(index % 3U) - 1.0F) * 1.55F;
            const float y = (static_cast<float>(index / 3U) - 1.0F) * 1.25F;
            const cubey::Transform3D transform{
                .translation = {x, y, 0.0F},
                .scale = {0.48F, 0.48F, 0.48F},
            };
            const cubey::Entity cube = cubey::scene::create_renderable_entity_3d(
                setup, cubey::scene::RenderableEntity3DConfig{
                           .transform = transform,
                           .mesh = cube_mesh_handle_,
                           .material = material_handles_.at(index),
                           .local_bounds =
                               cubey::Bounds3D{
                                   .center = {0.0F, 0.0F, 0.0F},
                                   .half_extent = {1.0F, 1.0F, 1.0F},
                               },
                       });
            cubes_.push_back({
                .entity = cube,
                .material = material_handles_.at(index),
                .color = kMaterialColors.at(index),
            });
        }
        camera_entity_ = cubey::scene::create_camera_entity_3d(
            setup, cubey::orbit_camera_transform(cubey::OrbitCameraState{.distance = 7.0F}));
        setup.commit();
    }

    void update_camera_transform() {
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(
            camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                .distance = 7.0F,
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
        };
        cubey::scene::RenderFramePlan3D plan =
            cubey::scene::build_render_frame_plan_3d(render_view, view, engine_.render_resources());
        if (plan.draw_packets.size() != kMaterialCubeCount) {
            throw std::runtime_error("material_cubes scene should produce one packet per cube");
        }
        return plan;
    }

    void upload_material_uniforms(cubey::render::FrameSlot frame_slot) {
        for (const MaterialCube& cube : cubes_) {
            material_instances_.at(cube.material)
                .upload(frame_slot,
                        MaterialUniforms{.base_color = cubey::render::srgb_to_linear_rgba(
                                             cube.color)});
        }
    }

    void record_cube_frame(const cubey::host::WindowedRenderFrame& frame) {
        upload_material_uniforms(frame.frame_slot);

        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::RenderFramePlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        forward_pass().record_to_present(
            recorder, frame.color_target,
            [this, &frame_plan,
             frame_slot = frame.frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
                pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            forward_pass().pipeline().pipeline());
                cubey::scene::record_draw_packets_3d(
                    pass_recorder, frame_plan.draw_packets, meshes_,
                    {.material_pass = cubey::render::MaterialPassKind::ForwardColor},
                    [this, &frame_plan,
                     frame_slot](const cubey::vulkan::CommandRecorder& packet_recorder,
                                 const cubey::scene::RenderDrawPacket3D& packet) {
                        const auto& material = material_instances_.at(packet.material);
                        cubey::render::bind_material_instance(packet_recorder,
                                                              forward_pass().pipeline(),
                                                              material.material(), frame_slot);
                        packet_recorder.push_constants(
                            forward_pass().pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                            PushConstants{
                                .mvp =
                                    frame_plan.view_projection_matrix * packet.world_affine_matrix,
                                .model = packet.world_affine_matrix,
                            });
                    });
            });

        recorder.end("vkEndCommandBuffer material_cubes");
    }

    [[nodiscard]] cubey::Scene& scene() {
        if (scene_ == nullptr) {
            throw std::runtime_error("material_cubes scene is not initialized");
        }
        return *scene_;
    }

    void destroy_scene_if_needed() {
        if (scene_ == nullptr) {
            return;
        }
        engine_.destroy_scene(*scene_);
        scene_ = nullptr;
        cubes_.clear();
        camera_entity_ = {};
    }

    void destroy_material_resources() {
        for (const cubey::render::MaterialHandle material : material_handles_) {
            if (material_instances_.contains(material)) {
                material_instances_.erase(material);
            }
            if (engine_.render_resources().is_alive(material)) {
                engine_.render_resources().destroy_material(material);
            }
        }
        material_handles_.clear();
    }

    void destroy_render_handles() {
        cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_,
                                             cube_mesh_handle_);
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
    cubey::Entity camera_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    OrbitController orbit_controller_;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    cubey::render::MaterialResourceTable<
        cubey::render::FrameUniformMaterialInstance<MaterialUniforms>>
        material_instances_;
    std::vector<cubey::render::MaterialHandle> material_handles_;
    std::vector<MaterialCube> cubes_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace

int run_material_cubes(const RunConfig& config) {
    MaterialCubesApp app(config);
    return app.run();
}

} // namespace cubey::examples::material_cubes
