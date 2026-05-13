#include "textured_cube_app.h"

#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/glfw_window.h>
#include <cubey/host/windowed_host.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/shader_bytecode.h>
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

using cubey::host::FrameStatsSample;
using cubey::vulkan::vk_struct;

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
        if (config_.headless) {
            throw std::runtime_error("textured_cube does not support --headless yet");
        }

        cubey::host::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
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
                    [this](cubey::host::WindowedAppContext& context) {
                        orbit_controller_.set_auto_rotation_speed(0.9F);
                        std::printf("textured_cube: %s rendering interactive compute shaded "
                                    "textured cube at %ux%u\n",
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
                        update_scene_transform();
                    },
                .record_frame =
                    [this](cubey::host::WindowedAppContext& context,
                           const cubey::host::WindowedRenderFrame& frame) {
                        (void)context;
                        record_cube_frame(frame);
                    },
                .frame_stats_sample =
                    [](cubey::host::WindowedAppContext& context,
                       const FrameTiming& timing) -> std::optional<FrameStatsSample> {
                    const VkExtent2D extent = context.swapchain().extent();
                    return FrameStatsSample{
                        .delta_seconds = timing.delta_seconds,
                        .width = extent.width,
                        .height = extent.height,
                        .triangles = kCubeTriangleCount,
                    };
                },
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
        if (meshes_.contains(cube_mesh_handle_)) {
            return;
        }
        cube_mesh_handle_ = engine_.render_resources().create_mesh("textured_cube.cube");
        cube_material_handle_ =
            engine_.render_resources().create_material("textured_cube.material");
        create_cube_mesh(context);
        create_scene();
        create_scene_uniforms(context);
        create_texture_resources(context);
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
        destroy_descriptors();
        destroy_compute_resources();
        destroy_scene_if_needed();
        destroy_render_handles();
        texture_.reset();
        scene_uniforms_.reset();
    }

    void create_pipeline(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("textured_cube.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("textured_cube.frag.spv"),
            },
        };
        const cubey::render::ShaderProgram shader_program(context.device(), shader_stage_files);

        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_uv_input_layout();

        const cubey::render::MaterialPassInfo material_pass =
            textured_cube_forward_pass_info();
        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        pipeline_resource_.emplace(
            context.device(), cubey::render::GraphicsPipelineResourceConfig{
                                  .extent = context.swapchain().extent(),
                                  .color_format = context.swapchain().format(),
                                  .depth_format = depth_attachment().format(),
                                  .shader_stages = shader_program.stages(),
                                  .vertex_bindings = vertex_input.bindings(),
                                  .vertex_attributes = vertex_input.attribute_descriptions(),
                                  .descriptor_set_layouts = set_layouts,
                                  .material_pass = material_pass,
                              });
    }

    void create_depth_resources(cubey::host::WindowedAppContext& context) {
        depth_attachment_.emplace(context.device(), context.swapchain().extent());
    }

    void create_cube_mesh(cubey::host::WindowedAppContext& context) {
        const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
            cubey::render::make_cube_position_color_normal_uv_mesh({
                .face_colors = kCubeFaceColors,
            });
        meshes_.emplace(cube_mesh_handle_, context.gpu(), cube.mesh_config());
    }

    void create_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        cube_entity_ = setup.entities().create();
        camera_entity_ = setup.entities().create();
        light_entity_ = setup.entities().create();
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
        setup.lights3d().create(
            light_entity_,
            cubey::directional_light_3d({0.35F, -0.55F, 0.76F}, {0.76F, 0.76F, 0.76F}, 1.0F));
        setup.commit();
    }

    void create_scene_uniforms(cubey::host::WindowedAppContext& context) {
        scene_uniforms_.emplace(context.device(), context.frame_slot_count());
    }

    void create_texture_resources(cubey::host::WindowedAppContext& context) {
        validate_texture_format_support(context);

        cubey::render::Texture2DConfig texture_config;
        texture_config.extent = {kTextureWidth, kTextureHeight};
        texture_config.format = kTextureFormat;
        texture_config.usage = cubey::render::Texture2DUsage::StorageSampled;
        texture_config.create_sampler = true;
        texture_.emplace(context.device(), texture_config);

        create_compute_resources(context);
        dispatch_compute_texture(context);
        destroy_compute_resources();

        create_descriptors(context);
    }

    static void validate_texture_format_support(cubey::host::WindowedAppContext& context) {
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

    static void transition_texture_image(cubey::host::WindowedAppContext& context,
                                         const cubey::vulkan::ImageLayoutTransition& transition) {
        static_cast<void>(context.gpu().submit_and_wait(cubey::vulkan::GpuWorkRequest{
            .label = "textured cube texture transition",
            .work =
                [transition](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context.device(),
                                                              gpu_context.submission());
                    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                    recorder.transition_image_layout(transition);
                    commands.submit_and_wait();
                },
        }));
    }

    void create_compute_resources(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
        compute_descriptors_.emplace(context.device(), descriptor_info);

        cubey::vulkan::DescriptorWriteBatch descriptor_writes;
        descriptor_writes.storage_image(compute_descriptors().set(), 0, texture().view());
        descriptor_writes.update(context.device());

        const std::array<VkDescriptorSetLayout, 1> set_layouts{compute_descriptors().layout()};
        const cubey::vulkan::PipelineLayoutInfo pipeline_layout_info({
            .set_layouts = set_layouts,
            .push_constants = {},
        });
        compute_pipeline_layout_.emplace(context.device(), pipeline_layout_info.create_info());

        const std::vector<std::uint32_t> compute_code =
            cubey::vulkan::read_spirv_file(shader_path("textured_cube.comp.spv"));
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

    void dispatch_compute_texture(cubey::host::WindowedAppContext& context) const {
        transition_texture_image(
            context, cubey::vulkan::begin_storage_image_write_transition(texture().handle()));

        constexpr std::uint32_t groups_x =
            (kTextureWidth + kTextureComputeGroupSize - 1U) / kTextureComputeGroupSize;
        constexpr std::uint32_t groups_y =
            (kTextureHeight + kTextureComputeGroupSize - 1U) / kTextureComputeGroupSize;
        static_cast<void>(context.gpu().submit_and_wait(cubey::vulkan::GpuWorkRequest{
            .label = "textured cube compute texture dispatch",
            .work =
                [this](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context.device(),
                                                              gpu_context.submission());
                    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                                           compute_pipeline().handle());
                    const VkDescriptorSet descriptor_set = compute_descriptors().set();
                    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                                 compute_pipeline_layout().handle(), 0,
                                                 descriptor_set);
                    recorder.dispatch(groups_x, groups_y, 1);
                    commands.submit_and_wait();
                },
        }));

        transition_texture_image(
            context,
            cubey::vulkan::finish_storage_image_write_for_sampling_transition(texture().handle()));
    }

    void create_descriptors(cubey::host::WindowedAppContext& context) {
        const cubey::render::MaterialPassInfo material_pass =
            textured_cube_forward_pass_info();
        const std::uint32_t frame_slot_count = context.frame_slot_count();
        const cubey::vulkan::DescriptorSetInfo descriptor_info(
            cubey::render::material_descriptor_set_info(material_pass, 0, frame_slot_count));
        descriptors_.emplace(context.device(), descriptor_info);

        cubey::vulkan::DescriptorWriteBatch descriptor_writes;
        for (std::uint32_t slot_index = 0; slot_index < frame_slot_count; ++slot_index) {
            const cubey::render::FrameSlot frame_slot{
                .index = slot_index,
                .count = frame_slot_count,
            };
            const VkDescriptorSet descriptor_set = descriptors().set(slot_index);
            descriptor_writes
                .uniform_buffer(descriptor_set, 0, scene_uniforms().buffer(frame_slot).handle(),
                                scene_uniforms().range())
                .combined_image_sampler(descriptor_set, 1, texture().sampler().handle(),
                                        texture().view());
        }
        descriptor_writes.update(context.device());
    }

    void destroy_descriptors() {
        descriptors_.reset();
    }

    void update_scene_transform() {
        const cubey::Transform3D transform{
            .rotation = cubey::math::euler_xyz_quat(
                {orbit_controller_.pitch(), orbit_controller_.yaw(), 0.0F}),
        };
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(cube_entity_, transform);
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
        scene_uniforms().upload(frame_slot, uniforms);
    }

    [[nodiscard]] cubey::scene::RenderFramePlan3D
    current_frame_plan(const cubey::SceneReadView& view, VkExtent2D extent) const {
        const cubey::scene::View3D render_view{
            .camera_entity = camera_entity_,
            .width = extent.width,
            .height = extent.height,
            .environment =
                cubey::scene::Environment3D{
                    .ambient_color = {0.24F, 0.24F, 0.24F},
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

        recorder.transition_image_layout(
            cubey::vulkan::begin_color_attachment_transition(frame.color_target.image));
        recorder.transition_image_layout(
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

        recorder.begin_rendering(rendering.info());
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource().pipeline());
        const VkDescriptorSet descriptor_set = descriptors().set(frame.frame_slot.index);
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_resource().layout(),
                                     0, descriptor_set);
        const cubey::render::RenderItem render_item =
            cubey::scene::render_item_from_packet(draw_packet);
        const cubey::render::DrawItem draw_item =
            cubey::render::resolve_draw_item(render_item, meshes_);
        cubey::render::record_draw_item(recorder.handle(), draw_item);
        recorder.end_rendering();

        recorder.transition_image_layout(
            cubey::vulkan::finish_color_attachment_for_present_transition(
                frame.color_target.image));

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

    [[nodiscard]] const cubey::render::FrameUniformBuffer<SceneUniforms>& scene_uniforms() const {
        if (!scene_uniforms_.has_value()) {
            throw std::runtime_error("scene uniforms are not initialized");
        }
        return scene_uniforms_.value();
    }

    [[nodiscard]] const cubey::render::Texture2D& texture() const {
        if (!texture_.has_value()) {
            throw std::runtime_error("texture is not initialized");
        }
        return texture_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetArray& descriptors() const {
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

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline_resource() const {
        if (!pipeline_resource_.has_value()) {
            throw std::runtime_error("pipeline resource is not initialized");
        }
        return pipeline_resource_.value();
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
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity cube_entity_;
    cubey::Entity camera_entity_;
    cubey::Entity light_entity_;
    cubey::render::MeshHandle cube_mesh_handle_{};
    cubey::render::MaterialHandle cube_material_handle_{};
    OrbitController orbit_controller_;

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    std::optional<cubey::render::FrameUniformBuffer<SceneUniforms>> scene_uniforms_;
    std::optional<cubey::render::Texture2D> texture_;
    std::optional<cubey::vulkan::DescriptorSetArray> descriptors_;
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_resource_;
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
