#include "pbr_furnace_app.h"

#include "pbr_furnace_scene.h"

#include <cubey/engine/engine.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/resource_table.h>
#include <cubey/render/texture.h>
#include <cubey/scene/render_recording.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <glm/geometric.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef CUBEY_PBR_FURNACE_SHADER_DIR
#error "CUBEY_PBR_FURNACE_SHADER_DIR must be defined by the pbr_furnace CMake target"
#endif

namespace cubey::projects::pbr_furnace {
namespace {

using cubey::host::FrameStatsSample;

constexpr float kSphereRadius = 0.46F;
constexpr std::uint32_t kIblPrefilteredExtent = 64;
constexpr std::uint32_t kIblPrefilteredMipLevels = 5;
constexpr VkFormat kIblFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

struct WhitePbrEnvironment {
    cubey::render::TextureCube irradiance_cube;
    cubey::render::TextureCube prefiltered_cube;
    cubey::render::Texture2D brdf_lut;
    std::uint32_t prefiltered_mip_levels = 1;
    float intensity = 1.0F;
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PBR_FURNACE_SHADER_DIR) / filename;
}

void append_rgba32f(std::vector<std::uint8_t>& bytes, std::array<float, 4> rgba) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(float) * rgba.size());
    std::memcpy(bytes.data() + offset, rgba.data(), sizeof(float) * rgba.size());
}

std::vector<std::uint8_t> white_cube_bytes(std::uint32_t extent, std::uint32_t mip_levels) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(cubey::render::texture_cube_byte_size(
        extent, mip_levels, cubey::render::texture_format_byte_size(kIblFormat)));
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const std::uint32_t mip_extent = cubey::render::texture_cube_mip_extent(extent, mip);
        const std::size_t texel_count =
            static_cast<std::size_t>(mip_extent) * static_cast<std::size_t>(mip_extent) * 6U;
        for (std::size_t texel = 0; texel < texel_count; ++texel) {
            append_rgba32f(bytes, {1.0F, 1.0F, 1.0F, 1.0F});
        }
    }
    return bytes;
}

WhitePbrEnvironment create_white_pbr_environment(const cubey::vulkan::Device& device,
                                                 cubey::vulkan::GpuRuntime& gpu) {
    const cubey::render::GeneratedPbrEnvironmentConfig brdf_config{
        .irradiance_extent = 1,
        .prefiltered_extent = 1,
        .prefiltered_mip_levels = 1,
        .brdf_lut_extent = 128,
        .intensity = 1.0F,
    };
    const cubey::render::GeneratedPbrEnvironmentData brdf_data =
        cubey::render::generate_pbr_environment_data(brdf_config);
    const cubey::vulkan::SamplerConfig sampler{
        .min_filter = VK_FILTER_LINEAR,
        .mag_filter = VK_FILTER_LINEAR,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .max_lod = static_cast<float>(kIblPrefilteredMipLevels - 1U),
    };
    std::vector<std::uint8_t> irradiance_bytes = white_cube_bytes(16, 1);
    std::vector<std::uint8_t> prefiltered_bytes =
        white_cube_bytes(kIblPrefilteredExtent, kIblPrefilteredMipLevels);
    cubey::render::TextureCube irradiance = cubey::render::create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = 16,
            .mip_levels = 1,
            .format = kIblFormat,
            .bytes = std::span<const std::uint8_t>{irradiance_bytes.data(),
                                                   irradiance_bytes.size()},
            .create_sampler = true,
            .sampler = sampler,
        });
    cubey::render::TextureCube prefiltered = cubey::render::create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = kIblPrefilteredExtent,
            .mip_levels = kIblPrefilteredMipLevels,
            .format = kIblFormat,
            .bytes = std::span<const std::uint8_t>{prefiltered_bytes.data(),
                                                   prefiltered_bytes.size()},
            .create_sampler = true,
            .sampler = sampler,
        });
    cubey::render::Texture2D brdf_lut = cubey::render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {brdf_config.brdf_lut_extent, brdf_config.brdf_lut_extent},
            .format = kIblFormat,
            .bytes = std::span<const std::uint8_t>{brdf_data.brdf_lut_rgba32f.data(),
                                                   brdf_data.brdf_lut_rgba32f.size()},
            .create_sampler = true,
            .sampler = sampler,
        });
    return {
        .irradiance_cube = std::move(irradiance),
        .prefiltered_cube = std::move(prefiltered),
        .brdf_lut = std::move(brdf_lut),
        .prefiltered_mip_levels = kIblPrefilteredMipLevels,
        .intensity = 1.0F,
    };
}

cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> make_pbr_sphere_mesh() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> sphere =
        cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
            .radius = kSphereRadius,
            .latitude_segments = 24,
            .longitude_segments = 48,
        });

    cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> mesh;
    mesh.vertices.reserve(sphere.vertices.size());
    mesh.indices = sphere.indices;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : sphere.vertices) {
        const cubey::math::Vec3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        cubey::math::Vec3 tangent = glm::cross(cubey::math::Vec3{0.0F, 1.0F, 0.0F}, normal);
        if (glm::length(tangent) < 0.0001F) {
            tangent = {1.0F, 0.0F, 0.0F};
        } else {
            tangent = glm::normalize(tangent);
        }
        mesh.vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = normal,
            .tangent = {tangent.x, tangent.y, tangent.z, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return mesh;
}

} // namespace

class PbrFurnaceApp {
  public:
    explicit PbrFurnaceApp(RunConfig config) : config_(std::move(config)) {}

    PbrFurnaceApp(const PbrFurnaceApp&) = delete;
    PbrFurnaceApp& operator=(const PbrFurnaceApp&) = delete;

    int run() {
        if (config_.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
            create_forward_pass(context.device(), context.swapchain().extent(),
                                context.swapchain().format());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
            update_camera_transform();
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext&,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_furnace_frame(frame.command_buffer, frame.color_target, frame.frame_slot, true);
        };
        callbacks.frame_stats_sample =
            [](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            const VkExtent2D extent = context.swapchain().extent();
            return FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = extent.width,
                .height = extent.height,
                .triangles = static_cast<std::uint32_t>(kPbrFurnaceMaterialCount) * 24U * 48U * 2U,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "pbr_furnace",
                .ready_status = "rendering PBR white furnace",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(), 1);
            create_forward_pass(context.device(), context.render_target().extent,
                                context.render_target().format);
        };
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            record_furnace_frame(command_buffer, target,
                                 cubey::render::FrameSlot{.index = 0, .count = 1}, false);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (scene_ != nullptr) {
            return;
        }
        create_default_textures(device, gpu);
        dummy_shadow_.emplace(create_solid_texture(device, gpu, {255, 255, 255, 255},
                                                   VK_FORMAT_R8G8B8A8_UNORM));
        white_environment_.emplace(create_white_pbr_environment(device, gpu));
        create_scene_material(device, frame_slot_count);
        create_materials(device, frame_slot_count);
        create_mesh(gpu);
        create_scene();
    }

    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("pbr_furnace.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("pbr_furnace.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::pbr_vertex_input_layout();
        const std::array<VkDescriptorSetLayout, 2> set_layouts{
            scene_material().layout(),
            material_instances_.at(material_handles_.front()).layout(),
        };
        forward_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{
                .extent = extent,
                .color_format = color_format,
            },
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .descriptor_set_layouts = set_layouts,
                        .material_pass = cubey::render::pbr_forward_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.018F, 0.018F, 0.018F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });
    }

    void destroy_swapchain_resources() {
        forward_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        scene_material_.reset();
        destroy_scene_if_needed();
        destroy_material_resources();
        cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_,
                                             sphere_mesh_handle_);
        white_environment_.reset();
        dummy_shadow_.reset();
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

    void create_scene_material(const cubey::vulkan::Device& device,
                               std::uint32_t frame_slot_count) {
        const auto binding = [](cubey::render::PbrSceneBinding value) {
            return static_cast<std::uint32_t>(value);
        };
        const WhitePbrEnvironment& environment = white_environment();
        scene_material_.emplace(device, cubey::render::FrameUniformMaterialInstanceConfig{
                                            .material_pass = cubey::render::pbr_forward_pass_info(),
                                            .descriptor_set = 0,
                                            .frame_slot_count = frame_slot_count,
                                            .uniform_binding =
                                                binding(cubey::render::PbrSceneBinding::
                                                            SceneUniforms),
                                            .sampled_images =
                                                {
                                                    cubey::render::SampledImageMaterialBinding{
                                                        .binding = binding(cubey::render::
                                                                               PbrSceneBinding::
                                                                                   ShadowMap),
                                                        .sampler =
                                                            dummy_shadow().sampler().handle(),
                                                        .image_view = dummy_shadow().view(),
                                                    },
                                                    cubey::render::SampledImageMaterialBinding{
                                                        .binding = binding(cubey::render::
                                                                               PbrSceneBinding::
                                                                                   IrradianceCube),
                                                        .sampler = environment.irradiance_cube
                                                                       .sampler()
                                                                       .handle(),
                                                        .image_view =
                                                            environment.irradiance_cube.view(),
                                                    },
                                                    cubey::render::SampledImageMaterialBinding{
                                                        .binding = binding(cubey::render::
                                                                               PbrSceneBinding::
                                                                                   PrefilteredCube),
                                                        .sampler = environment.prefiltered_cube
                                                                       .sampler()
                                                                       .handle(),
                                                        .image_view =
                                                            environment.prefiltered_cube.view(),
                                                    },
                                                    cubey::render::SampledImageMaterialBinding{
                                                        .binding = binding(cubey::render::
                                                                               PbrSceneBinding::
                                                                                   BrdfLut),
                                                        .sampler =
                                                            environment.brdf_lut.sampler().handle(),
                                                        .image_view = environment.brdf_lut.view(),
                                                    },
                                                },
                                        });
    }

    void create_materials(const cubey::vulkan::Device& device, std::uint32_t frame_slot_count) {
        const auto materials = pbr_furnace_material_grid();
        material_handles_.reserve(materials.size());
        for (const PbrFurnaceMaterial& furnace_material : materials) {
            const cubey::render::MaterialHandle material =
                engine_.render_resources().create_material(cubey::render::MaterialInfo{
                    .label = "pbr_furnace.material.r" + std::to_string(furnace_material.row) +
                             ".c" + std::to_string(furnace_material.column),
                    .sort_key =
                        (furnace_material.row * kPbrFurnaceColumnCount) + furnace_material.column,
                });
            material_handles_.push_back(material);
            material_factors_.emplace(
                material, cubey::render::PbrMaterialFactors{
                              .base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F},
                              .metallic_factor = furnace_material.metallic,
                              .roughness_factor = furnace_material.roughness,
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
        const cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> mesh =
            make_pbr_sphere_mesh();
        sphere_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
            engine_.render_resources(), meshes_, gpu, "pbr_furnace.sphere", mesh);
    }

    void create_scene() {
        scene_ = &engine_.create_scene();
        cubey::SceneTransaction setup = scene().begin_transaction();
        const auto materials = pbr_furnace_material_grid();
        for (std::size_t index = 0; index < materials.size(); ++index) {
            const PbrFurnaceMaterial& material = materials[index];
            static_cast<void>(cubey::scene::create_renderable_entity_3d(
                setup,
                {
                    .transform = cubey::Transform3D{
                        .translation = material.position,
                    },
                    .mesh = sphere_mesh_handle_,
                    .material = material_handles_.at(index),
                    .local_bounds =
                        cubey::Bounds3D{
                            .center = {0.0F, 0.0F, 0.0F},
                            .half_extent = {kSphereRadius, kSphereRadius, kSphereRadius},
                        },
                    .cast_shadows = false,
                    .receive_shadows = false,
                }));
        }
        camera_entity_ = cubey::scene::create_camera_entity_3d(
            setup,
            cubey::orbit_camera_transform(cubey::OrbitCameraState{
                .target = {0.0F, 0.0F, 0.0F},
                .distance = 9.0F,
            }));
        setup.commit();
    }

    void update_camera_transform() {
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        edits.transforms3d().set_local_transform(
            camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                .target = {0.0F, 0.0F, 0.0F},
                                .distance = 9.0F,
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
                    .ambient_color = {0.0F, 0.0F, 0.0F},
                    .ambient_intensity = 0.0F,
                },
            .culling_enabled = false,
        };
        cubey::scene::RenderFramePlan3D plan =
            cubey::scene::build_render_frame_plan_3d(render_view, view, engine_.render_resources());
        if (plan.draw_packets.size() != kPbrFurnaceMaterialCount) {
            throw std::runtime_error("pbr_furnace scene should produce one packet per material");
        }
        return plan;
    }

    [[nodiscard]] cubey::render::PbrSceneUniforms
    scene_uniforms(const cubey::SceneReadView& scene_view,
                   const cubey::scene::RenderFramePlan3D& plan, VkFormat color_format) const {
        const cubey::math::Vec3 camera_position = camera_world_position(scene_view);
        return {
            .view_projection = plan.view_projection_matrix,
            .light_view_projection = cubey::math::Mat4{1.0F},
            .camera_position = {camera_position, 1.0F},
            .light_direction = {0.0F, -1.0F, 0.0F, 0.0F},
            .light_color_intensity = {1.0F, 1.0F, 1.0F, 0.0F},
            .ambient_color_intensity = {0.0F, 0.0F, 0.0F, 0.0F},
            .environment_intensity_mip_count =
                {
                    white_environment().intensity,
                    static_cast<float>(white_environment().prefiltered_mip_levels),
                    0.0F,
                    0.0F,
                },
            .display_transform = cubey::render::pbr_display_transform_uniform(
                cubey::render::pbr_display_transform_for_target(
                    color_format, 0.0F, cubey::render::PbrTonemap::Linear)),
        };
    }

    [[nodiscard]] cubey::math::Vec3 camera_world_position(const cubey::SceneReadView& view) const {
        const cubey::TransformInstance3D instance = view.transforms3d().instance(camera_entity_);
        const cubey::math::Mat4& world = view.transforms3d().world_affine_matrix(instance);
        return {world[3].x, world[3].y, world[3].z};
    }

    void record_furnace_frame(VkCommandBuffer command_buffer,
                              cubey::render::ColorTargetView color_target,
                              cubey::render::FrameSlot frame_slot, bool present) {
        cubey::SceneReadView scene_view = scene().read();
        const cubey::scene::RenderFramePlan3D frame_plan =
            current_frame_plan(scene_view, color_target.extent);
        scene_material().upload(frame_slot,
                                scene_uniforms(scene_view, frame_plan, color_target.format));

        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        if (present) {
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        }
        const auto record = [this, &frame_plan,
                             frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::scene::record_pipeline_draw_packets_3d(
                pass_recorder, frame_plan.draw_packets, meshes_,
                {
                    .pipeline = &forward_pass().pipeline(),
                    .material = &scene_material().material(),
                    .frame_slot = frame_slot,
                    .filter =
                        {
                            .material_pass = cubey::render::MaterialPassKind::ForwardColor,
                            .blend_mode = cubey::render::MaterialBlendMode::Opaque,
                        },
                },
                [this, frame_slot](const cubey::vulkan::CommandRecorder& packet_recorder,
                                    const cubey::scene::RenderDrawPacket3D& packet) {
                    const auto& material = material_instances_.at(packet.material);
                    material.upload(frame_slot,
                                    cubey::render::pbr_material_uniforms(
                                        material_factors_.at(packet.material)));
                    cubey::render::bind_material_instance(packet_recorder, forward_pass().pipeline(),
                                                          material.material(), frame_slot);
                    packet_recorder.push_constants(
                        forward_pass().pipeline().layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                        cubey::render::pbr_push_constants(packet.world_affine_matrix));
                });
        };
        if (present) {
            forward_pass().record_to_present(recorder, color_target, record);
            recorder.end("vkEndCommandBuffer pbr_furnace");
        } else {
            forward_pass().record_to_target(recorder, color_target, record);
        }
    }

    [[nodiscard]] cubey::Scene& scene() {
        if (scene_ == nullptr) {
            throw std::runtime_error("pbr_furnace scene is not initialized");
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
            throw std::runtime_error("PBR furnace default texture is not initialized");
        }
        return texture->value();
    }

    [[nodiscard]] const cubey::render::Texture2D& dummy_shadow() const {
        if (!dummy_shadow_.has_value()) {
            throw std::runtime_error("PBR furnace dummy shadow texture is not initialized");
        }
        return dummy_shadow_.value();
    }

    [[nodiscard]] const WhitePbrEnvironment& white_environment() const {
        if (!white_environment_.has_value()) {
            throw std::runtime_error("PBR furnace white environment is not initialized");
        }
        return white_environment_.value();
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<
        cubey::render::PbrSceneUniforms>&
    scene_material() const {
        if (!scene_material_.has_value()) {
            throw std::runtime_error("PBR furnace scene material is not initialized");
        }
        return scene_material_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("PBR furnace forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    cubey::Engine engine_;
    cubey::Scene* scene_ = nullptr;
    cubey::Entity camera_entity_{};
    cubey::OrbitController orbit_controller_;
    cubey::render::MeshHandle sphere_mesh_handle_{};

    cubey::render::MeshResourceTable<cubey::render::Mesh> meshes_;
    cubey::render::MaterialResourceTable<
        cubey::render::FrameUniformMaterialInstance<cubey::render::PbrMaterialUniforms>>
        material_instances_;
    std::vector<cubey::render::MaterialHandle> material_handles_;
    std::unordered_map<cubey::render::MaterialHandle, cubey::render::PbrMaterialFactors,
                       cubey::render::MaterialHandleHash>
        material_factors_;
    std::optional<cubey::render::Texture2D> base_color_default_;
    std::optional<cubey::render::Texture2D> metallic_roughness_default_;
    std::optional<cubey::render::Texture2D> normal_default_;
    std::optional<cubey::render::Texture2D> occlusion_default_;
    std::optional<cubey::render::Texture2D> emissive_default_;
    std::optional<cubey::render::Texture2D> dummy_shadow_;
    std::optional<WhitePbrEnvironment> white_environment_;
    std::optional<
        cubey::render::FrameUniformMaterialInstance<cubey::render::PbrSceneUniforms>>
        scene_material_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

int run_pbr_furnace(const RunConfig& config) {
    PbrFurnaceApp app(config);
    return app.run();
}

} // namespace cubey::projects::pbr_furnace
