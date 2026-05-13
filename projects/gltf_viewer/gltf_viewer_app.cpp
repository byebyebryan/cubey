#include "gltf_viewer_app.h"

#include <cubey/asset/gltf_asset.h>
#include <cubey/core/math.h>
#include <cubey/engine/engine.h>
#include <cubey/engine/gltf_scene_importer.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/shadow_map.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/render_recording.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image.h>

#include <vulkan/vulkan.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef CUBEY_GLTF_VIEWER_SHADER_DIR
#error "CUBEY_GLTF_VIEWER_SHADER_DIR must be defined by the gltf_viewer CMake target"
#endif

namespace cubey::projects::gltf_viewer {
namespace {

using cubey::host::FrameStatsSample;

constexpr std::uint32_t kShadowMapSize = 2048;
constexpr std::uint32_t kFallbackCubeTriangleCount = 12;
const cubey::math::Vec3 kLightDirection =
    glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

struct ShadowPushConstants {
    cubey::math::Mat4 light_mvp{1.0F};
};

static_assert(sizeof(ShadowPushConstants) == sizeof(cubey::math::Mat4));
static_assert(std::is_trivially_copyable_v<cubey::render::PbrSceneUniforms>);

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_GLTF_VIEWER_SHADER_DIR) / filename;
}

std::filesystem::path bundled_sample_asset_path() {
#ifdef CUBEY_GLTF_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_GLTF_SAMPLE_ASSETS_DIR) / "Models" / "DamagedHelmet" /
           "glTF-Binary" / "DamagedHelmet.glb";
#else
    return {};
#endif
}

cubey::render::RenderGraphTextureState undefined_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

cubey::render::RenderGraphTextureState sampled_depth_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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

std::vector<cubey::render::PbrVertex> fallback_cube_vertices() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();
    std::vector<cubey::render::PbrVertex> vertices;
    vertices.reserve(cube.vertices.size());
    for (const cubey::render::VertexPositionColorNormalUv& vertex : cube.vertices) {
        vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return vertices;
}

std::vector<std::uint32_t> fallback_cube_indices() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();
    std::vector<std::uint32_t> indices;
    indices.reserve(cube.indices.size());
    for (const std::uint16_t index : cube.indices) {
        indices.push_back(index);
    }
    return indices;
}

} // namespace

class GltfViewerApp {
  public:
    explicit GltfViewerApp(RunConfig config) : config_(std::move(config)) {}

    GltfViewerApp(const GltfViewerApp&) = delete;
    GltfViewerApp& operator=(const GltfViewerApp&) = delete;

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
            record_viewer_frame(context, frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            const VkExtent2D extent = context.swapchain().extent();
            return FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = extent.width,
                .height = extent.height,
                .triangles = triangle_count_,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "gltf_viewer",
                .ready_status = "rendering glTF/PBR viewer",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

  private:
    struct ViewerRenderGraph {
        cubey::render::CompiledRenderGraph graph;
    };

    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
        if (scene_ != nullptr) {
            return;
        }

        const std::filesystem::path input = resolved_input_path();
        if (!input.empty()) {
            asset_.emplace(cubey::asset::load_gltf_asset(input));
            create_imported_asset_scene(context, asset_.value());
        } else {
            create_default_textures(context);
            create_fallback_material(context);
            create_fallback_mesh(context);
            scene_bounds_ = {
                .center = {0.0F, 0.0F, 0.0F},
                .half_extent = {1.0F, 1.0F, 1.0F},
            };
            create_fallback_scene();
        }

        create_shadow_resources(context);
        create_scene_material(context);
    }

    void create_swapchain_resources(cubey::host::WindowedAppContext& context) {
        depth_attachment_.emplace(context.device(), context.swapchain().extent());
        graph_executor_.clear();
        graph_executor_.resize(context.frame_slot_count());
        create_forward_pipeline(context);
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        forward_pipeline_.reset();
        depth_attachment_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        scene_material_.reset();
        shadow_pass_.reset();
        destroy_scene_if_needed();
        cubey::destroy_gltf_scene_import(engine_, import_resources_, import_result_);
        triangle_count_ = 0;
        normal_default_.reset();
        metallic_roughness_default_.reset();
        emissive_default_.reset();
        occlusion_default_.reset();
        base_color_default_.reset();
        asset_.reset();
        shadow_depth_is_sampled_ = false;
    }

    void create_imported_asset_scene(cubey::host::WindowedAppContext& context,
                                     const cubey::asset::GltfAsset& asset) {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        import_result_ = cubey::import_gltf_scene(
            engine_, setup, asset, context.device(), context.gpu(), import_resources_,
            cubey::GltfSceneImportConfig{
                .label_prefix = "gltf_viewer",
            });
        scene_bounds_ = import_result_.bounds;
        triangle_count_ = import_result_.triangle_count;
        create_camera_and_light(setup);
        setup.commit();
    }

    [[nodiscard]] std::filesystem::path resolved_input_path() const {
        if (!config_.input_path.empty()) {
            if (!std::filesystem::exists(config_.input_path)) {
                throw std::runtime_error("input glTF asset does not exist: " +
                                         config_.input_path.string());
            }
            return config_.input_path;
        }

        const std::filesystem::path sample = bundled_sample_asset_path();
        if (!sample.empty() && std::filesystem::exists(sample)) {
            return sample;
        }
        return {};
    }

    void create_default_textures(cubey::host::WindowedAppContext& context) {
        base_color_default_.emplace(create_solid_texture(context, {255, 255, 255, 255},
                                                         VK_FORMAT_R8G8B8A8_SRGB));
        metallic_roughness_default_.emplace(create_solid_texture(context, {255, 255, 0, 255},
                                                                 VK_FORMAT_R8G8B8A8_UNORM));
        normal_default_.emplace(create_solid_texture(context, {128, 128, 255, 255},
                                                     VK_FORMAT_R8G8B8A8_UNORM));
        occlusion_default_.emplace(create_solid_texture(context, {255, 255, 255, 255},
                                                        VK_FORMAT_R8G8B8A8_UNORM));
        emissive_default_.emplace(create_solid_texture(context, {0, 0, 0, 255},
                                                       VK_FORMAT_R8G8B8A8_SRGB));
    }

    [[nodiscard]] cubey::render::Texture2D
    create_solid_texture(cubey::host::WindowedAppContext& context,
                         std::array<std::uint8_t, 4> color, VkFormat format) {
        return cubey::render::create_uploaded_texture_2d(
            context.device(), context.gpu(),
            {
                .extent = {1, 1},
                .format = format,
                .rgba8 = std::span<const std::uint8_t>{color.data(), color.size()},
                .create_sampler = true,
                .sampler = {},
            });
    }

    void create_fallback_material(cubey::host::WindowedAppContext& context) {
        const cubey::render::MaterialHandle material =
            engine_.render_resources().create_material("gltf_viewer.fallback.material");
        import_result_.material_handles.push_back(material);
        import_result_.first_material_handle = material;
        import_resources_.material_factors.emplace(
            material, cubey::render::PbrMaterialFactors{
                          .base_color_factor = {0.86F, 0.82F, 0.72F, 1.0F},
                          .metallic_factor = 0.0F,
                          .roughness_factor = 0.58F,
                      });
        cubey::render::MaterialInstance& instance =
            import_resources_.material_instances.emplace(
                material, context.device(),
                cubey::render::MaterialInstanceConfig{
                    .material_pass = cubey::render::pbr_forward_pass_info(),
                    .descriptor_set = 1,
                });
        cubey::render::MaterialDescriptorWriter(instance.set())
            .combined_image_sampler(
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::BaseColor),
                default_texture(cubey::render::PbrMaterialBinding::BaseColor).sampler().handle(),
                default_texture(cubey::render::PbrMaterialBinding::BaseColor).view())
            .combined_image_sampler(
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::MetallicRoughness),
                default_texture(cubey::render::PbrMaterialBinding::MetallicRoughness)
                    .sampler()
                    .handle(),
                default_texture(cubey::render::PbrMaterialBinding::MetallicRoughness).view())
            .combined_image_sampler(
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Normal),
                default_texture(cubey::render::PbrMaterialBinding::Normal).sampler().handle(),
                default_texture(cubey::render::PbrMaterialBinding::Normal).view())
            .combined_image_sampler(
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Occlusion),
                default_texture(cubey::render::PbrMaterialBinding::Occlusion).sampler().handle(),
                default_texture(cubey::render::PbrMaterialBinding::Occlusion).view())
            .combined_image_sampler(
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Emissive),
                default_texture(cubey::render::PbrMaterialBinding::Emissive).sampler().handle(),
                default_texture(cubey::render::PbrMaterialBinding::Emissive).view())
            .update(context.device());
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
        }
        if (texture == nullptr || !texture->has_value()) {
            throw std::runtime_error("default PBR texture is not initialized");
        }
        return texture->value();
    }

    void create_fallback_mesh(cubey::host::WindowedAppContext& context) {
        std::vector<cubey::render::PbrVertex> vertices = fallback_cube_vertices();
        std::vector<std::uint32_t> indices = fallback_cube_indices();
        const cubey::render::MeshHandle mesh =
            engine_.render_resources().create_mesh("gltf_viewer.fallback.cube");
        import_resources_.meshes.emplace(
            mesh, context.gpu(),
            cubey::render::indexed_mesh_config(std::span<const cubey::render::PbrVertex>{vertices},
                                               std::span<const std::uint32_t>{indices}));
        import_result_.mesh_handles.push_back(mesh);
        import_resources_.mesh_primitives = {{
            cubey::GltfImportedPrimitive3D{
                .mesh = mesh,
                .material = import_result_.first_material_handle,
                .local_bounds =
                    {
                        .center = {0.0F, 0.0F, 0.0F},
                        .half_extent = {1.0F, 1.0F, 1.0F},
                    },
            },
        }};
        triangle_count_ = kFallbackCubeTriangleCount;
        import_result_.triangle_count = triangle_count_;
        import_result_.bounds = {
            .center = {0.0F, 0.0F, 0.0F},
            .half_extent = {1.0F, 1.0F, 1.0F},
        };
        import_resources_.active = true;
    }

    void create_fallback_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        cubey::Entity cube = setup.entities().create();
        setup.transforms3d().create(cube, {});
        setup.renderables3d().create(
            cube,
            cubey::Renderable3D{
                .primitives =
                    {
                        cubey::RenderablePrimitive3D{
                            .mesh = import_resources_.mesh_primitives.front().front().mesh,
                            .material = import_result_.first_material_handle,
                        },
                    },
                .local_bounds = import_resources_.mesh_primitives.front().front().local_bounds,
            });
        import_result_.root_entities.push_back(cube);
        create_camera_and_light(setup);
        setup.commit();
    }

    void create_camera_and_light(cubey::SceneTransaction& setup) {
        const float radius = std::max(glm::length(scene_bounds_.half_extent), 1.0F);
        camera_distance_ = std::max(radius * 2.8F, 4.2F);
        camera_entity_ = cubey::scene::create_camera_entity_3d(
            setup, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                       .target = scene_bounds_.center,
                       .distance = camera_distance_,
                   }),
            cubey::Camera3D({
                .near_z = std::max(radius * 0.001F, 0.01F),
                .far_z = std::max(radius * 12.0F, 100.0F),
            }));

        const cubey::math::Vec3 light_eye =
            scene_bounds_.center + (kLightDirection * std::max(radius * 4.0F, 6.0F));
        light_camera_entity_ = cubey::scene::create_camera_entity_3d(
            setup, look_at_transform(light_eye, scene_bounds_.center),
            cubey::Camera3D({
                .projection = cubey::Camera3DProjection::Orthographic,
                .orthographic_height = std::max(radius * 3.0F, 4.0F),
                .near_z = 0.1F,
                .far_z = std::max(radius * 10.0F, 16.0F),
            }));

        cubey::Light3D sunlight =
            cubey::directional_light_3d(kLightDirection, {1.0F, 0.94F, 0.82F}, 2.2F);
        sunlight.casts_shadows = true;
        light_entity_ = cubey::scene::create_directional_light_entity_3d(setup, sunlight);
    }

    void create_shadow_resources(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 1> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("gltf_shadow_depth.vert.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_only_input_layout(sizeof(cubey::render::PbrVertex));
        const VkPushConstantRange push_constant_range{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(ShadowPushConstants),
        };
        shadow_pass_.emplace(
            context.device(),
            cubey::render::ShadowMapPass3DConfig{
                .extent = {kShadowMapSize, kShadowMapSize},
                .pipeline =
                    {
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .material_pass = cubey::render::shadow_depth_pass_info({
                            .label = "gltf_viewer.shadow",
                            .push_constants =
                                std::span<const VkPushConstantRange>{&push_constant_range, 1},
                        }),
                    },
                .sampler = {},
            });
    }

    void create_scene_material(cubey::host::WindowedAppContext& context) {
        scene_material_.emplace(context.device(), cubey::render::FrameUniformMaterialInstanceConfig{
                                                      .material_pass =
                                                          cubey::render::pbr_forward_pass_info(),
                                                      .descriptor_set = 0,
                                                      .frame_slot_count =
                                                          context.frame_slot_count(),
                                                      .uniform_binding = static_cast<std::uint32_t>(
                                                          cubey::render::PbrSceneBinding::
                                                              SceneUniforms),
                                                      .sampled_images =
                                                          {
                                                              cubey::render::
                                                                  SampledImageMaterialBinding{
                                                                      .binding =
                                                                          static_cast<std::uint32_t>(
                                                                              cubey::render::
                                                                                  PbrSceneBinding::
                                                                                      ShadowMap),
                                                                      .sampler = shadow_pass()
                                                                                     .depth_texture()
                                                                                     .sampler()
                                                                                     .handle(),
                                                                      .image_view = shadow_pass()
                                                                                        .depth_texture()
                                                                                        .view(),
                                                                      .layout =
                                                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                                                  },
                                                          },
                                                  });
    }

    void create_forward_pipeline(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("gltf_pbr.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("gltf_pbr.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::pbr_vertex_input_layout();
        const std::array<VkDescriptorSetLayout, 2> set_layouts{
            scene_material().layout(),
            import_resources_.material_instances.at(import_result_.first_material_handle).layout(),
        };
        forward_pipeline_.emplace(
            context.device(),
            cubey::render::graphics_pipeline_file_resource_config(
                {
                    .extent = context.swapchain().extent(),
                    .color_format = context.swapchain().format(),
                    .depth_format = depth_attachment().format(),
                },
                {
                    .shader_stage_files = shader_stage_files,
                    .vertex_bindings = vertex_input.bindings(),
                    .vertex_attributes = vertex_input.attribute_descriptions(),
                    .descriptor_set_layouts = set_layouts,
                    .material_pass = cubey::render::pbr_forward_pass_info(),
                }));
    }

    void update_camera_transform() {
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(
            camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                .target = scene_bounds_.center,
                                .distance = camera_distance_,
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
                    .ambient_color = {0.04F, 0.045F, 0.055F},
                    .ambient_intensity = 1.0F,
                },
        };
        return cubey::scene::FrameRenderPlan3D({
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
    }

    [[nodiscard]] ViewerRenderGraph
    current_render_graph(const cubey::host::WindowedRenderFrame& frame,
                         const cubey::scene::RenderFramePlan3D& shadow_plan,
                         const cubey::scene::RenderFramePlan3D& scene_plan) {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "backbuffer", frame.color_target, undefined_texture_state(), present_texture_state());
        const cubey::render::RenderGraphTextureHandle scene_depth =
            graph.import_depth_target("scene depth", cubey::render::depth_target_view(
                                                         depth_attachment()),
                                      undefined_texture_state());
        const std::optional<cubey::render::RenderGraphTextureState> shadow_initial_state =
            shadow_depth_is_sampled_ ? sampled_depth_texture_state() : undefined_texture_state();
        const cubey::render::RenderGraphTextureHandle shadow_depth =
            graph.import_depth_target("shadow depth", shadow_pass().depth_target(),
                                      shadow_initial_state);

        graph.add_pass("shadow", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_depth(shadow_depth)
            .material_pass(shadow_pass().material_pass())
            .execute([this, &shadow_plan](
                         const cubey::render::RenderGraphExecutionContext& context) {
                record_shadow_pass(context.recorder(), shadow_plan);
            });
        graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(shadow_depth)
            .write_color(backbuffer)
            .write_depth(scene_depth)
            .material_pass(cubey::render::pbr_forward_pass_info())
            .execute([this, &frame, &scene_plan](
                         const cubey::render::RenderGraphExecutionContext& context) {
                record_scene_pass(context.recorder(), frame.color_target, scene_plan,
                                  frame.frame_slot);
            });

        return {
            .graph = graph.compile(),
        };
    }

    void record_viewer_frame(cubey::host::WindowedAppContext& context,
                             const cubey::host::WindowedRenderFrame& frame) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::FrameRenderPlan3D frame_plan =
            current_frame_plan(scene_view, frame.color_target.extent);
        if (frame_plan.passes().size() != 2) {
            throw std::runtime_error("gltf_viewer frame plan should have two passes");
        }
        const cubey::scene::RenderFramePlan3D& shadow_plan = frame_plan.passes()[0].frame_plan;
        const cubey::scene::RenderFramePlan3D& scene_plan = frame_plan.passes()[1].frame_plan;
        scene_material().upload(frame.frame_slot,
                                scene_uniforms(scene_view, scene_plan, shadow_plan));

        const ViewerRenderGraph render_graph = current_render_graph(frame, shadow_plan, scene_plan);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &context.device(),
                .command_buffer = frame.command_buffer,
                .frame_slot = frame.frame_slot,
                .label = "vkEndCommandBuffer gltf_viewer",
            },
            render_graph.graph);
        shadow_depth_is_sampled_ = true;
    }

    [[nodiscard]] cubey::render::PbrSceneUniforms
    scene_uniforms(const cubey::SceneReadView& scene_view,
                   const cubey::scene::RenderFramePlan3D& scene_plan,
                   const cubey::scene::RenderFramePlan3D& shadow_plan) const {
        const cubey::math::Vec3 camera_position = camera_world_position(scene_view, camera_entity_);
        const cubey::LightPacket3D light = current_light_packet(scene_plan);
        const cubey::math::Vec3 ambient =
            scene_plan.environment.ambient_color * scene_plan.environment.ambient_intensity;
        return {
            .view_projection = scene_plan.view_projection_matrix,
            .light_view_projection = shadow_plan.view_projection_matrix,
            .camera_position = {camera_position, 1.0F},
            .light_direction = {light.direction, 0.0F},
            .light_color_intensity = {light.color, light.intensity},
            .ambient_color_intensity = {ambient, 1.0F},
        };
    }

    [[nodiscard]] cubey::math::Vec3 camera_world_position(const cubey::SceneReadView& view,
                                                          cubey::Entity camera) const {
        const cubey::TransformInstance3D instance = view.transforms3d().instance(camera);
        const cubey::math::Mat4& world = view.transforms3d().world_affine_matrix(instance);
        return {world[3].x, world[3].y, world[3].z};
    }

    [[nodiscard]] cubey::LightPacket3D
    current_light_packet(const cubey::scene::RenderFramePlan3D& plan) const {
        for (const cubey::LightPacket3D& light : plan.light_packets) {
            if (light.entity == light_entity_) {
                return light;
            }
        }
        return cubey::LightPacket3D{
            .entity = light_entity_,
            .kind = cubey::LightKind3D::Directional,
            .color = {1.0F, 0.94F, 0.82F},
            .intensity = 2.2F,
            .direction = kLightDirection,
        };
    }

    void record_shadow_pass(const cubey::vulkan::CommandRecorder& recorder,
                            const cubey::scene::RenderFramePlan3D& shadow_plan) const {
        shadow_pass().record(
            recorder, cubey::render::depth_clear_value(),
            [this, &shadow_plan](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::scene::record_pipeline_draw_packets_3d(
                    pass_recorder, shadow_plan.draw_packets, import_resources_.meshes,
                    {
                        .pipeline = &shadow_pass().pipeline(),
                        .filter =
                            {
                                .material_pass = cubey::render::MaterialPassKind::DepthOnly,
                                .require_shadow_caster = true,
                            },
                    },
                    [this, &shadow_plan](const cubey::vulkan::CommandRecorder& packet_recorder,
                                         const cubey::scene::RenderDrawPacket3D& packet) {
                        packet_recorder.push_constants(
                            shadow_pass().pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                            ShadowPushConstants{
                                .light_mvp =
                                    shadow_plan.view_projection_matrix *
                                    packet.world_affine_matrix,
                            });
                    });
            });
    }

    void record_scene_pass(const cubey::vulkan::CommandRecorder& recorder,
                           cubey::render::ColorTargetView color_target,
                           const cubey::scene::RenderFramePlan3D& scene_plan,
                           cubey::render::FrameSlot frame_slot) const {
        cubey::render::record_render_target_pass(
            recorder,
            cubey::render::render_target_view(color_target,
                                              cubey::render::depth_target_view(depth_attachment())),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.018F, 0.020F, 0.026F, 1.0F),
                .depth = cubey::render::depth_clear_value(),
            },
            [this, &scene_plan, frame_slot](
                const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::scene::record_pipeline_draw_packets_3d(
                    pass_recorder, scene_plan.draw_packets, import_resources_.meshes,
                    {
                        .pipeline = &forward_pipeline(),
                        .material = &scene_material().material(),
                        .frame_slot = frame_slot,
                        .filter =
                            {
                                .material_pass = cubey::render::MaterialPassKind::ForwardColor,
                            },
                    },
                    [this](const cubey::vulkan::CommandRecorder& packet_recorder,
                           const cubey::scene::RenderDrawPacket3D& packet) {
                        const cubey::render::MaterialInstance& material =
                            import_resources_.material_instances.at(packet.material);
                        cubey::render::bind_material_instance(packet_recorder, forward_pipeline(),
                                                              material);
                        packet_recorder.push_constants(
                            forward_pipeline().layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            cubey::render::pbr_push_constants(
                                packet.world_affine_matrix,
                                import_resources_.material_factors.at(packet.material)));
                    });
            });
    }

    [[nodiscard]] cubey::Scene& scene() {
        if (scene_ == nullptr) {
            throw std::runtime_error("gltf_viewer scene is not initialized");
        }
        return *scene_;
    }

    [[nodiscard]] const cubey::Scene& scene() const {
        if (scene_ == nullptr) {
            throw std::runtime_error("gltf_viewer scene is not initialized");
        }
        return *scene_;
    }

    void destroy_scene_if_needed() {
        if (scene_ == nullptr) {
            return;
        }
        engine_.destroy_scene(*scene_);
        scene_ = nullptr;
        camera_entity_ = {};
        light_camera_entity_ = {};
        light_entity_ = {};
    }

    [[nodiscard]] const cubey::render::ShadowMapPass3D& shadow_pass() const {
        if (!shadow_pass_.has_value()) {
            throw std::runtime_error("shadow pass is not initialized");
        }
        return shadow_pass_.value();
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<
        cubey::render::PbrSceneUniforms>&
    scene_material() const {
        if (!scene_material_.has_value()) {
            throw std::runtime_error("PBR scene material is not initialized");
        }
        return scene_material_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& forward_pipeline() const {
        if (!forward_pipeline_.has_value()) {
            throw std::runtime_error("PBR forward pipeline is not initialized");
        }
        return forward_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const {
        if (!depth_attachment_.has_value()) {
            throw std::runtime_error("scene depth attachment is not initialized");
        }
        return depth_attachment_.value();
    }

    RunConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    std::optional<cubey::asset::GltfAsset> asset_{};
    cubey::Entity camera_entity_{};
    cubey::Entity light_camera_entity_{};
    cubey::Entity light_entity_{};
    cubey::Bounds3D scene_bounds_{};
    float camera_distance_ = 4.2F;
    cubey::OrbitController orbit_controller_;
    bool shadow_depth_is_sampled_ = false;
    std::uint32_t triangle_count_ = 0;

    cubey::GltfSceneImportResources import_resources_{};
    cubey::GltfSceneImportResult import_result_{};
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    std::optional<cubey::render::Texture2D> base_color_default_;
    std::optional<cubey::render::Texture2D> metallic_roughness_default_;
    std::optional<cubey::render::Texture2D> normal_default_;
    std::optional<cubey::render::Texture2D> occlusion_default_;
    std::optional<cubey::render::Texture2D> emissive_default_;
    std::optional<cubey::render::ShadowMapPass3D> shadow_pass_;
    std::optional<
        cubey::render::FrameUniformMaterialInstance<cubey::render::PbrSceneUniforms>>
        scene_material_;
    std::optional<cubey::render::GraphicsPipelineResource> forward_pipeline_;
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_;
};

int run_gltf_viewer(const RunConfig& config) {
    GltfViewerApp app(config);
    return app.run();
}

} // namespace cubey::projects::gltf_viewer
