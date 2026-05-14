#include "textured_cube_app.h"

#include "../common/cube_scene.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/generated_texture.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/render_recording.h>
#include <cubey/scene/scene.h>
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
#include <utility>
#include <vector>

#ifndef CUBEY_TEXTURED_CUBE_SHADER_DIR
#error "CUBEY_TEXTURED_CUBE_SHADER_DIR must be defined by the textured_cube CMake target"
#endif

namespace cubey::examples::textured_cube {
namespace {

using cubey::host::FrameStatsSample;

constexpr std::uint32_t kTextureWidth = 64;
constexpr std::uint32_t kTextureHeight = 64;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kTextureComputeGroupSize = 8;
constexpr std::uint32_t kCubeTriangleCount = 12;

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

cubey::render::MaterialPassInfo textured_cube_forward_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "textured_cube.forward",
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
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 1,
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .depth_test = true,
        .depth_write = true,
    };
}

constexpr std::array<cubey::render::PrimitiveVec3, 6> kCubeFaceColors{
    cubey::render::PrimitiveVec3{1.0F, 0.92F, 0.86F},
    cubey::render::PrimitiveVec3{0.86F, 0.94F, 1.0F},
    cubey::render::PrimitiveVec3{0.9F, 1.0F, 0.9F},
    cubey::render::PrimitiveVec3{1.0F, 0.96F, 0.78F},
    cubey::render::PrimitiveVec3{0.96F, 0.9F, 1.0F},
    cubey::render::PrimitiveVec3{0.86F, 1.0F, 1.0F},
};

class TexturedCubeApp {
  public:
    explicit TexturedCubeApp(RunConfig config) : config_(std::move(config)) {}

    TexturedCubeApp(const TexturedCubeApp&) = delete;
    TexturedCubeApp& operator=(const TexturedCubeApp&) = delete;

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
            orbit_controller_.set_auto_rotation_speed(0.9F);
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
            update_scene_transform(timing);
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
                .triangles = kCubeTriangleCount,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "textured_cube",
                .ready_status = "rendering interactive compute shaded textured cube",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
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
            engine_.render_resources().create_material("textured_cube.material");
        cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
            engine_.render_resources(), meshes_, context.gpu(), "textured_cube.cube",
            cubey::render::make_cube_position_color_normal_uv_mesh({
                .face_colors = kCubeFaceColors,
            }));
        create_scene();
        create_texture_resources(context);
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
        texture_.reset();
        material_.reset();
    }

    void create_forward_pass(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("textured_cube.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("textured_cube.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_uv_input_layout();
        const std::array<VkDescriptorSetLayout, 1> set_layouts{material().layout()};
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
                        .material_pass = textured_cube_forward_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.014F, 0.016F, 0.022F, 1.0F),
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
                           .camera_distance = 4.2F,
                           .directional_light = cubey::directional_light_3d(
                               {0.35F, -0.55F, 0.76F}, {0.72F, 0.72F, 0.72F}, 1.0F),
                       });
        cube_entity_ = cube_scene.cube;
        camera_entity_ = cube_scene.camera;
        light_entity_ = cube_scene.light;
        setup.commit();
    }

    void create_texture_resources(cubey::host::WindowedAppContext& context) {
        texture_.emplace(cubey::render::create_compute_generated_texture_2d(
            context.device(), context.gpu(),
            {
                .label = "textured cube compute texture dispatch",
                .extent = {kTextureWidth, kTextureHeight},
                .format = kTextureFormat,
                .shader = cubey::render::compute_shader_file(shader_path("textured_cube.comp.spv")),
                .group_size_x = kTextureComputeGroupSize,
                .group_size_y = kTextureComputeGroupSize,
                .group_size_z = 1,
                .create_sampler = true,
            }));
        material_.emplace(context.device(), cubey::render::FrameUniformMaterialInstanceConfig{
                                                .material_pass = textured_cube_forward_pass_info(),
                                                .descriptor_set = 0,
                                                .frame_slot_count = context.frame_slot_count(),
                                                .uniform_binding = 0,
                                                .sampled_images =
                                                    {
                                                        cubey::render::SampledImageMaterialBinding{
                                                            .binding = 1,
                                                            .sampler = texture().sampler().handle(),
                                                            .image_view = texture().view(),
                                                        },
                                                    },
                                            });
    }

    void update_scene_transform(const FrameTiming& timing) {
        const float seconds = static_cast<float>(timing.elapsed_seconds);
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(
            cube_entity_, cubey::examples::common::cube_spin_transform(seconds));
        edits.transforms3d().set_local_transform(
            camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                .distance = 4.2F,
                                .yaw = orbit_controller_.yaw(),
                                .pitch = orbit_controller_.pitch(),
                            }));
        scene().commit(edits);
    }

    [[nodiscard]] SceneUniforms
    current_scene_uniforms(const cubey::scene::RenderFramePlan3D& plan,
                           const cubey::scene::RenderDrawPacket3D& packet) const {
        const cubey::LightPacket3D light = current_light_packet(plan);
        const cubey::math::Vec3 ambient =
            plan.environment.ambient_color * plan.environment.ambient_intensity;

        return {
            .mvp = plan.view_projection_matrix * packet.world_affine_matrix,
            .model = packet.world_affine_matrix,
            .light_direction = {light.direction.x, light.direction.y, light.direction.z, 0.0F},
            .light_color = {light.color.x * light.intensity, light.color.y * light.intensity,
                            light.color.z * light.intensity, 1.0F},
            .ambient_color = {ambient.x, ambient.y, ambient.z, 1.0F},
        };
    }

    [[nodiscard]] cubey::LightPacket3D
    current_light_packet(const cubey::scene::RenderFramePlan3D& plan) const {
        for (const cubey::LightPacket3D& light : plan.light_packets) {
            if (light.entity == light_entity_) {
                if (light.kind != cubey::LightKind3D::Directional) {
                    throw std::runtime_error("textured_cube scene light should be directional");
                }
                return light;
            }
        }
        throw std::runtime_error("textured_cube scene should produce one directional light packet");
    }

    void update_scene_uniforms(const cubey::scene::RenderFramePlan3D& plan,
                               const cubey::scene::RenderDrawPacket3D& packet,
                               cubey::render::FrameSlot frame_slot) {
        const SceneUniforms uniforms = current_scene_uniforms(plan, packet);
        material().upload(frame_slot, uniforms);
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
            throw std::runtime_error("textured_cube scene should produce one draw packet");
        }
        return plan;
    }

    void record_cube_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::RenderFramePlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        const cubey::scene::RenderDrawPacket3D& draw_packet = frame_plan.draw_packets[0];
        update_scene_uniforms(frame_plan, draw_packet, frame.frame_slot);

        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        forward_pass().record_to_present_target(recorder, frame.color_target,
                                         [this, &frame_plan, frame_slot = frame.frame_slot](
                                             const cubey::vulkan::CommandRecorder& pass_recorder) {
                                             cubey::scene::record_pipeline_draw_packets_3d(
                                                 pass_recorder, frame_plan.draw_packets, meshes_,
                                                 {
                                                     .pipeline = &forward_pass().pipeline(),
                                                     .material = &material().material(),
                                                     .frame_slot = frame_slot,
                                                 },
                                                 [](const cubey::vulkan::CommandRecorder&,
                                                    const cubey::scene::RenderDrawPacket3D&) {});
                                         });

        recorder.end("vkEndCommandBuffer textured_cube");
    }

    [[nodiscard]] cubey::Scene& scene() {
        if (scene_ == nullptr) {
            throw std::runtime_error("textured_cube scene is not initialized");
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
        light_entity_ = {};
    }

    void destroy_render_handles() {
        cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_,
                                             cube_mesh_handle_);
        if (engine_.render_resources().is_alive(cube_material_handle_)) {
            engine_.render_resources().destroy_material(cube_material_handle_);
            cube_material_handle_ = {};
        }
    }

    [[nodiscard]] const cubey::render::Texture2D& texture() const {
        if (!texture_.has_value()) {
            throw std::runtime_error("texture is not initialized");
        }
        return texture_.value();
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<SceneUniforms>&
    material() const {
        if (!material_.has_value()) {
            throw std::runtime_error("material is not initialized");
        }
        return material_.value();
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
    cubey::Entity light_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MaterialHandle cube_material_handle_{};
    OrbitController orbit_controller_;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::optional<cubey::render::Texture2D> texture_;
    std::optional<cubey::render::FrameUniformMaterialInstance<SceneUniforms>> material_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace

int run_textured_cube(const RunConfig& config) {
    TexturedCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::textured_cube
