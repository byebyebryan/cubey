#include "material_cubes_app.h"

#include "../common/cube_scene.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/render_recording.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>

#include <vulkan/vulkan.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef CUBEY_MATERIAL_CUBES_SHADER_DIR
#error "CUBEY_MATERIAL_CUBES_SHADER_DIR must be defined by the material_cubes CMake target"
#endif

namespace cubey::examples::material_cubes {
namespace {

using cubey::host::FrameStatsSample;

constexpr std::uint32_t kMaterialGridColumns = 7;
constexpr std::uint32_t kMaterialGridRows = 5;
constexpr std::uint32_t kMaterialCubeCount = kMaterialGridColumns * kMaterialGridRows;
constexpr std::uint32_t kCubeTriangleCount = 12;
constexpr std::uint32_t kShadowMapSize = 2048;
constexpr float kCameraDistance = 8.8F;
constexpr float kMaterialGridSpacingX = 1.16F;
constexpr float kMaterialGridSpacingY = 1.12F;
constexpr cubey::math::Vec3 kMaterialCubeScale{0.42F, 0.42F, 0.42F};
constexpr cubey::math::Vec4 kNeutralMaterialBaseColor{0.56F, 0.55F, 0.52F, 1.0F};
constexpr float kMinimumRoughness = 0.04F;
const cubey::math::Vec3 kLightDirection =
    glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_MATERIAL_CUBES_SHADER_DIR) / filename;
}

cubey::ForwardPbrRenderer3DConfig forward_pbr_renderer_3d_config() {
    return {
        .pbr_vertex_shader = shader_path("gltf_pbr.vert.spv"),
        .pbr_fragment_shader = shader_path("gltf_pbr.frag.spv"),
        .skybox_vertex_shader = shader_path("gltf_skybox.vert.spv"),
        .skybox_fragment_shader = shader_path("gltf_skybox.frag.spv"),
        .post_vertex_shader = shader_path("pbr_post.vert.spv"),
        .post_fragment_shader = shader_path("pbr_post.frag.spv"),
        .shadow_depth_vertex_shader = shader_path("gltf_shadow_depth.vert.spv"),
        .shadow_extent = kShadowMapSize,
    };
}

cubey::render::RenderGraphTextureState undefined_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

cubey::render::RenderGraphTextureState present_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };
}

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

struct MaterialVariant {
    cubey::math::Vec4 base_color = kNeutralMaterialBaseColor;
    float roughness = 1.0F;
    float metallic = 0.0F;
};

[[nodiscard]] float material_grid_t(std::uint32_t index, std::uint32_t count) {
    if (count <= 1U) {
        return 0.0F;
    }
    return static_cast<float>(index) / static_cast<float>(count - 1U);
}

[[nodiscard]] MaterialVariant material_variant_for_cell(std::uint32_t row,
                                                        std::uint32_t column) {
    const float column_t = material_grid_t(column, kMaterialGridColumns);
    const float row_t = material_grid_t(row, kMaterialGridRows);
    return {
        .base_color = kNeutralMaterialBaseColor,
        .roughness = kMinimumRoughness + ((1.0F - kMinimumRoughness) * column_t),
        .metallic = row_t,
    };
}

[[nodiscard]] MaterialVariant material_variant_for_index(std::uint32_t index) {
    return material_variant_for_cell(index / kMaterialGridColumns, index % kMaterialGridColumns);
}

[[nodiscard]] cubey::math::Vec3 material_cube_translation(std::uint32_t index) {
    const std::uint32_t row = index / kMaterialGridColumns;
    const std::uint32_t column = index % kMaterialGridColumns;
    const float centered_column =
        static_cast<float>(column) - (static_cast<float>(kMaterialGridColumns - 1U) * 0.5F);
    const float centered_row =
        (static_cast<float>(kMaterialGridRows - 1U) * 0.5F) - static_cast<float>(row);
    return {
        centered_column * kMaterialGridSpacingX,
        centered_row * kMaterialGridSpacingY,
        0.0F,
    };
}

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::PbrVertex>
make_pbr_cube_mesh() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();

    cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> result;
    result.vertices.reserve(cube.vertices.size());
    result.indices = cube.indices;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : cube.vertices) {
        result.vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return result;
}

struct MaterialCube {
    cubey::Entity entity{};
    cubey::render::MaterialHandle material{};
};

class MaterialCubesApp {
  public:
    explicit MaterialCubesApp(RunConfig config) : config_(std::move(config)) {}

    MaterialCubesApp(const MaterialCubesApp&) = delete;
    MaterialCubesApp& operator=(const MaterialCubesApp&) = delete;

    int run() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
            create_swapchain_resources(context.device(), context.swapchain().extent(),
                                       context.swapchain().format());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
            update_scene_transform(timing);
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_cube_frame(context, frame);
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
    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (scene_ != nullptr) {
            return;
        }
        create_default_textures(device, gpu);
        create_materials(device, frame_slot_count);
        create_mesh(gpu);
        create_scene();
        create_ibl_resources(device, gpu);
        forward_pbr_renderer_ =
            &engine_.renderers().create_forward_pbr_renderer_3d(forward_pbr_renderer_3d_config());
        forward_pbr_renderer().create_global_resources(device, ibl_environment(), frame_slot_count);
    }

    void create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                    VkFormat color_format) {
        forward_pbr_renderer().create_swapchain_resources(
            device, cubey::ForwardPbrRenderer3DTargetResourcesInfo{
                        .extent = extent,
                        .color_format = color_format,
                        .material_descriptor_set_layout = material_descriptor_set_layout(),
                    });
    }

    void destroy_swapchain_resources() {
        engine_.renderers().destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        engine_.renderers().destroy_all_resources();
        forward_pbr_renderer_ = nullptr;
        ibl_environment_.reset();
        destroy_scene_if_needed();
        destroy_material_resources();
        destroy_render_handles();
        normal_default_.reset();
        metallic_roughness_default_.reset();
        emissive_default_.reset();
        occlusion_default_.reset();
        base_color_default_.reset();
    }

    void create_default_textures(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu) {
        base_color_default_.emplace(create_solid_texture(device, gpu, {255, 255, 255, 255},
                                                         VK_FORMAT_R8G8B8A8_SRGB));
        metallic_roughness_default_.emplace(create_solid_texture(device, gpu, {255, 255, 255, 255},
                                                                 VK_FORMAT_R8G8B8A8_UNORM));
        normal_default_.emplace(create_solid_texture(device, gpu, {128, 128, 255, 255},
                                                     VK_FORMAT_R8G8B8A8_UNORM));
        occlusion_default_.emplace(create_solid_texture(device, gpu, {255, 255, 255, 255},
                                                        VK_FORMAT_R8G8B8A8_UNORM));
        emissive_default_.emplace(create_solid_texture(device, gpu, {0, 0, 0, 255},
                                                       VK_FORMAT_R8G8B8A8_SRGB));
    }

    [[nodiscard]] cubey::render::Texture2D
    create_solid_texture(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                         std::array<std::uint8_t, 4> color, VkFormat format) {
        return cubey::render::create_uploaded_texture_2d(
            device, gpu,
            {
                .extent = {1, 1},
                .format = format,
                .rgba8 = std::span<const std::uint8_t>{color.data(), color.size()},
                .create_sampler = true,
                .sampler = {},
            });
    }

    void create_materials(const cubey::vulkan::Device& device, std::uint32_t frame_slot_count) {
        material_handles_.reserve(kMaterialCubeCount);
        for (std::uint32_t index = 0; index < kMaterialCubeCount; ++index) {
            const MaterialVariant variant = material_variant_for_index(index);
            const cubey::render::MaterialHandle material =
                engine_.render_resources().create_material(cubey::render::MaterialInfo{
                    .label = "material_cubes.material." + std::to_string(index),
                    .sort_key = index,
                });
            material_handles_.push_back(material);
            material_factors_.emplace(
                material, cubey::render::PbrMaterialFactors{
                              .base_color_factor = variant.base_color,
                              .metallic_factor = variant.metallic,
                              .roughness_factor = variant.roughness,
                              .reflectance = 0.5F,
                          });
            material_instances_.emplace(
                material, device,
                cubey::render::FrameUniformMaterialInstanceConfig{
                    .material_pass = cubey::render::pbr_forward_pass_info(),
                    .descriptor_set = 1,
                    .frame_slot_count = frame_slot_count,
                    .uniform_binding =
                        static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Uniforms),
                    .sampled_images = material_sampled_images(),
                });
        }
    }

    [[nodiscard]] std::vector<cubey::render::SampledImageMaterialBinding>
    material_sampled_images() const {
        const auto sampled = [this](cubey::render::PbrMaterialBinding binding) {
            const cubey::render::Texture2D& texture = default_texture(binding);
            return cubey::render::SampledImageMaterialBinding{
                .binding = static_cast<std::uint32_t>(binding),
                .sampler = texture.sampler().handle(),
                .image_view = texture.view(),
            };
        };
        return {
            sampled(cubey::render::PbrMaterialBinding::BaseColor),
            sampled(cubey::render::PbrMaterialBinding::MetallicRoughness),
            sampled(cubey::render::PbrMaterialBinding::Normal),
            sampled(cubey::render::PbrMaterialBinding::Occlusion),
            sampled(cubey::render::PbrMaterialBinding::Emissive),
        };
    }

    void create_mesh(cubey::vulkan::GpuRuntime& gpu) {
        cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
            engine_.render_resources(), meshes_, gpu, "material_cubes.cube", make_pbr_cube_mesh());
    }

    void create_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        for (std::uint32_t index = 0; index < kMaterialCubeCount; ++index) {
            const cubey::Entity cube = cubey::scene::create_renderable_entity_3d(
                setup, cubey::scene::RenderableEntity3DConfig{
                           .transform =
                               cubey::Transform3D{
                                   .translation = material_cube_translation(index),
                                   .scale = kMaterialCubeScale,
                               },
                           .mesh = cube_mesh_handle_,
                           .material = material_handles_.at(index),
                           .local_bounds =
                               cubey::Bounds3D{
                                   .center = {0.0F, 0.0F, 0.0F},
                                   .half_extent = {1.0F, 1.0F, 1.0F},
                               },
                           .cast_shadows = false,
                           .receive_shadows = false,
                       });
            cubes_.push_back({
                .entity = cube,
                .material = material_handles_.at(index),
            });
        }

        camera_entity_ = cubey::scene::create_camera_entity_3d(
            setup, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                       .distance = kCameraDistance,
                   }));
        const cubey::math::Vec3 light_eye = kLightDirection * 9.0F;
        light_camera_entity_ = cubey::scene::create_camera_entity_3d(
            setup, look_at_transform(light_eye, {0.0F, 0.0F, 0.0F}),
            cubey::Camera3D({
                .projection = cubey::Camera3DProjection::Orthographic,
                .orthographic_height = 8.5F,
                .near_z = 0.1F,
                .far_z = 32.0F,
            }));
        cubey::Light3D light =
            cubey::directional_light_3d(kLightDirection, {1.0F, 0.94F, 0.82F}, 1.2F);
        light.casts_shadows = false;
        light_entity_ = cubey::scene::create_directional_light_entity_3d(setup, light);
        setup.commit();
    }

    void create_ibl_resources(const cubey::vulkan::Device& device,
                              cubey::vulkan::GpuRuntime& gpu) {
        cubey::render::GeneratedPbrEnvironmentConfig ibl_config;
        ibl_config.intensity = config_.ibl_intensity;

        if (!config_.environment_path.empty()) {
            if (!std::filesystem::exists(config_.environment_path)) {
                throw std::runtime_error("environment HDR does not exist: " +
                                         config_.environment_path.string());
            }
            const cubey::asset::HdrImage image = cubey::asset::load_hdr_image(
                config_.environment_path);
            ibl_environment_.emplace(cubey::render::create_pbr_environment_from_equirectangular(
                device, gpu,
                cubey::render::PbrEquirectangularImage{
                    .width = image.width,
                    .height = image.height,
                    .rgba32f = image.rgba32f,
                },
                ibl_config));
            return;
        }

        ibl_environment_.emplace(
            cubey::render::create_generated_pbr_environment(device, gpu, ibl_config));
    }

    void update_scene_transform(const FrameTiming& timing) {
        const float seconds = static_cast<float>(timing.elapsed_seconds);
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(
            camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                .distance = kCameraDistance,
                                .yaw = orbit_controller_.yaw(),
                                .pitch = orbit_controller_.pitch(),
                            }));
        for (std::uint32_t index = 0; index < cubes_.size(); ++index) {
            edits.transforms3d().set_local_transform(
                cubes_.at(index).entity,
                cubey::examples::common::cube_spin_transform(
                    seconds, material_cube_translation(index), kMaterialCubeScale));
        }
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
                    .ambient_color = {0.035F, 0.038F, 0.045F},
                    .ambient_intensity = 1.0F,
                },
            .culling_enabled = false,
        };
        cubey::scene::FrameRenderPlan3D frame_plan({
            cubey::scene::RenderPassPlan3D{
                .label = "shadow",
                .kind = cubey::scene::RenderPassKind3D::DepthOnly,
                .frame_plan = cubey::scene::build_render_frame_plan_3d(
                    shadow_view, view, engine_.render_resources()),
            },
            cubey::scene::RenderPassPlan3D{
                .label = "scene",
                .kind = cubey::scene::RenderPassKind3D::Color,
                .frame_plan = cubey::scene::build_render_frame_plan_3d(
                    scene_view, view, engine_.render_resources()),
            },
        });
        if (frame_plan.passes()[1].frame_plan.draw_packets.size() != kMaterialCubeCount) {
            throw std::runtime_error("material_cubes scene should produce one packet per cube");
        }
        return frame_plan;
    }

    void record_cube_frame(cubey::host::WindowedAppContext& context,
                           const cubey::host::WindowedRenderFrame& frame) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::FrameRenderPlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        if (frame_plan.passes().size() != 2) {
            throw std::runtime_error("material_cubes frame plan should have two passes");
        }
        const cubey::scene::RenderFramePlan3D& shadow_plan = frame_plan.passes()[0].frame_plan;
        const cubey::scene::RenderFramePlan3D& scene_plan = frame_plan.passes()[1].frame_plan;
        const cubey::ForwardPbrRenderer3DRenderRequest request{
            .target =
                {
                    .device = &context.device(),
                    .command_buffer = frame.command_buffer,
                    .color_target = frame.color_target,
                    .frame_slot = frame.frame_slot,
                    .color_initial_state = undefined_texture_state(),
                    .color_final_state = present_texture_state(),
                    .command_buffer_label = "vkEndCommandBuffer material_cubes",
                },
            .view =
                {
                    .scene = &scene_view,
                    .shadow_plan = &shadow_plan,
                    .scene_plan = &scene_plan,
                    .camera_entity = camera_entity_,
                    .light_entity = light_entity_,
                    .fallback_light = fallback_light_packet(),
                },
            .resources =
                {
                    .meshes = &meshes_,
                    .material_instances = &material_instances_,
                    .material_factors = &material_factors_,
                },
            .settings =
                {
                    .environment_rotation_degrees = config_.environment_rotation_degrees,
                    .exposure = config_.exposure,
                },
        };
        forward_pbr_renderer().record(request);
    }

    [[nodiscard]] cubey::LightPacket3D fallback_light_packet() const {
        return cubey::LightPacket3D{
            .entity = light_entity_,
            .kind = cubey::LightKind3D::Directional,
            .color = {1.0F, 0.94F, 0.82F},
            .intensity = 1.2F,
            .direction = kLightDirection,
        };
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
        light_camera_entity_ = {};
        light_entity_ = {};
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
        material_factors_.clear();
    }

    void destroy_render_handles() {
        cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_,
                                             cube_mesh_handle_);
    }

    [[nodiscard]] const cubey::render::Texture2D&
    default_texture(cubey::render::PbrMaterialBinding binding) const {
        const std::optional<cubey::render::Texture2D>* texture = nullptr;
        switch (binding) {
        case cubey::render::PbrMaterialBinding::BaseColor:
            texture = &base_color_default_;
            break;
        case cubey::render::PbrMaterialBinding::MetallicRoughness:
            texture = &metallic_roughness_default_;
            break;
        case cubey::render::PbrMaterialBinding::Normal:
            texture = &normal_default_;
            break;
        case cubey::render::PbrMaterialBinding::Occlusion:
            texture = &occlusion_default_;
            break;
        case cubey::render::PbrMaterialBinding::Emissive:
            texture = &emissive_default_;
            break;
        case cubey::render::PbrMaterialBinding::Uniforms:
            break;
        }
        if (texture == nullptr || !texture->has_value()) {
            throw std::runtime_error("material_cubes default PBR texture is not initialized");
        }
        return texture->value();
    }

    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const {
        if (!ibl_environment_.has_value()) {
            throw std::runtime_error("material_cubes PBR IBL environment is not initialized");
        }
        return ibl_environment_.value();
    }

    [[nodiscard]] cubey::ForwardPbrRenderer3D& forward_pbr_renderer() const {
        if (forward_pbr_renderer_ == nullptr) {
            throw std::runtime_error("material_cubes forward PBR renderer is not initialized");
        }
        return *forward_pbr_renderer_;
    }

    [[nodiscard]] VkDescriptorSetLayout material_descriptor_set_layout() const {
        return material_instances_.at(material_handles_.front()).layout();
    }

    RunConfig config_;
    cubey::Engine engine_;
    cubey::ForwardPbrRenderer3D* forward_pbr_renderer_ = nullptr;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity camera_entity_{};
    cubey::Entity light_camera_entity_{};
    cubey::Entity light_entity_{};
    OrbitController orbit_controller_;
    cubey::render::MeshHandle cube_mesh_handle_{};

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    cubey::render::MaterialResourceTable<
        cubey::render::FrameUniformMaterialInstance<cubey::render::PbrMaterialUniforms>>
        material_instances_;
    std::vector<cubey::render::MaterialHandle> material_handles_;
    std::unordered_map<cubey::render::MaterialHandle, cubey::render::PbrMaterialFactors,
                       cubey::render::MaterialHandleHash>
        material_factors_;
    std::vector<MaterialCube> cubes_;
    std::optional<cubey::render::Texture2D> base_color_default_;
    std::optional<cubey::render::Texture2D> metallic_roughness_default_;
    std::optional<cubey::render::Texture2D> normal_default_;
    std::optional<cubey::render::Texture2D> occlusion_default_;
    std::optional<cubey::render::Texture2D> emissive_default_;
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
};

} // namespace

int run_material_cubes(const RunConfig& config) {
    MaterialCubesApp app(config);
    return app.run();
}

} // namespace cubey::examples::material_cubes
