#include "terrain_shadertoy_ref_mesh.h"

#include <cubey/core/math.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_TERRAIN_SHADERTOY_REF_SHADER_DIR
#error "CUBEY_TERRAIN_SHADERTOY_REF_SHADER_DIR must be defined"
#endif

namespace cubey::projects::terrain_shadertoy_ref {
namespace {

constexpr std::uint32_t kHeightAtlasExtent = 2048U;
constexpr float kMeshDomainExtent = 512.0F;
constexpr float kReferenceFovyRadians = 1.1760052F;
constexpr float kReferenceNear = 0.1F;
constexpr float kReferenceFar = 240.0F;

struct SourcePushConstants {
    cubey::math::Vec4 resolution_time{};
    cubey::math::Vec4 mouse{};
};

struct BakePushConstants {
    cubey::math::Vec4 resolution_time{};
    cubey::math::Vec4 mouse{};
    cubey::math::Vec4 domain_center_extent{};
};

struct ReferenceCamera {
    cubey::math::Vec3 position{};
    cubey::math::Vec3 right{};
    cubey::math::Vec3 up{};
    cubey::math::Vec3 forward{};
};

struct ReferenceFrameUniforms {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 camera_position_time{};
    cubey::math::Vec4 camera_right{};
    cubey::math::Vec4 camera_up{};
    cubey::math::Vec4 camera_forward{};
    cubey::math::Vec4 domain_center_extent_surface{};
    cubey::math::Vec4 resolution_options{};
    cubey::math::Vec4 diagnostic_options{};
};

struct ReferenceGridVertex {
    std::array<float, 2> uv{};
};

struct ReferenceGridData {
    std::vector<ReferenceGridVertex> vertices{};
    std::vector<std::uint32_t> indices{};
};

static_assert(sizeof(SourcePushConstants) == 32U);
static_assert(sizeof(BakePushConstants) == 48U);
static_assert(sizeof(ReferenceFrameUniforms) == 176U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TERRAIN_SHADERTOY_REF_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialDescriptorSetLayout channel_descriptor_layout() {
    return cubey::render::sampled_texture_descriptor_set_layout(0, 2);
}

[[nodiscard]] cubey::render::MaterialPassInfo
source_fullscreen_pass_info(const char* label, std::uint32_t push_constant_size) {
    return {
        .label = label,
        .descriptor_sets = {channel_descriptor_layout()},
        .push_constants =
            {
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .offset = 0,
                    .size = push_constant_size,
                },
            },
    };
}

[[nodiscard]] cubey::render::MaterialDescriptorSetLayout frame_descriptor_layout() {
    return {
        .set = 0,
        .bindings =
            {
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = 0,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = 1,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = 2,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = 3,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            },
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo frame_material_pass_info() {
    return {
        .label = "terrain_shadertoy_ref.frame",
        .descriptor_sets = {frame_descriptor_layout()},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo sky_pass_info() {
    return {
        .label = "terrain_shadertoy_ref.sky",
        .descriptor_sets = {frame_descriptor_layout()},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo diagnostic_pass_info() {
    return {
        .label = "terrain_shadertoy_ref.atlas_diagnostic",
        .descriptor_sets = {frame_descriptor_layout()},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo mesh_pass_info() {
    return {
        .label = "terrain_shadertoy_ref.mesh",
        .descriptor_sets = {frame_descriptor_layout()},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] SourcePushConstants source_push_constants(VkExtent2D extent, float time_seconds) {
    return {
        .resolution_time =
            {
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                1.0F,
                time_seconds,
            },
        .mouse = {0.0F, 0.0F, 0.0F, 0.0F},
    };
}

[[nodiscard]] cubey::vulkan::ImageLayoutTransition finish_bake_for_sampling(VkImage image) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dst_access_mask = VK_ACCESS_SHADER_READ_BIT,
        .src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dst_stage_mask =
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
}

[[nodiscard]] ReferenceGridData make_reference_grid(std::uint32_t cells) {
    if (cells == 0U) {
        throw std::runtime_error("reference grid requires positive cell count");
    }
    const std::uint64_t row = static_cast<std::uint64_t>(cells) + 1U;
    const std::uint64_t vertex_count = row * row;
    const std::uint64_t index_count = static_cast<std::uint64_t>(cells) * cells * 6U;
    if (vertex_count > std::numeric_limits<std::uint32_t>::max() ||
        index_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("reference grid exceeds uint32 limits");
    }

    ReferenceGridData data;
    data.vertices.reserve(static_cast<std::size_t>(vertex_count));
    data.indices.reserve(static_cast<std::size_t>(index_count));
    for (std::uint32_t z = 0; z <= cells; ++z) {
        for (std::uint32_t x = 0; x <= cells; ++x) {
            data.vertices.push_back({
                .uv =
                    {
                        static_cast<float>(x) / static_cast<float>(cells),
                        static_cast<float>(z) / static_cast<float>(cells),
                    },
            });
        }
    }

    const std::uint32_t row_u32 = cells + 1U;
    for (std::uint32_t z = 0; z < cells; ++z) {
        for (std::uint32_t x = 0; x < cells; ++x) {
            const std::uint32_t i00 = z * row_u32 + x;
            const std::uint32_t i10 = i00 + 1U;
            const std::uint32_t i01 = i00 + row_u32;
            const std::uint32_t i11 = i01 + 1U;
            if (((x + z) & 1U) == 0U) {
                data.indices.insert(data.indices.end(), {i00, i10, i11, i00, i11, i01});
            } else {
                data.indices.insert(data.indices.end(), {i00, i10, i01, i10, i11, i01});
            }
        }
    }
    return data;
}

[[nodiscard]] cubey::math::Mat4 reference_view_projection(const ReferenceCamera& camera,
                                                          float aspect) {
    cubey::math::Mat4 camera_world{1.0F};
    camera_world[0] = cubey::math::Vec4(camera.right, 0.0F);
    camera_world[1] = cubey::math::Vec4(camera.up, 0.0F);
    camera_world[2] = cubey::math::Vec4(-camera.forward, 0.0F);
    camera_world[3] = cubey::math::Vec4(camera.position, 1.0F);
    return cubey::math::perspective(kReferenceFovyRadians, aspect, kReferenceNear, kReferenceFar) *
           glm::inverse(camera_world);
}

} // namespace

class MountainsMeshRenderer::Impl {
  public:
    void create_global_resources(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                                 const TerrainShadertoyRefConfig& config,
                                 VkExtent2D reference_extent,
                                 const cubey::render::Texture2D& channel_texture) {
        config_ = config;
        channel_texture_ = &channel_texture;
        camera_ = probe_camera(device, gpu, reference_extent);
        height_atlas_.emplace(device,
                              cubey::render::Texture2DConfig{
                                  .extent = {kHeightAtlasExtent, kHeightAtlasExtent},
                                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                  .usage = cubey::render::Texture2DUsage::ColorAttachmentSampled,
                                  .create_sampler = true,
                                  .sampler =
                                      {
                                          .min_filter = VK_FILTER_LINEAR,
                                          .mag_filter = VK_FILTER_LINEAR,
                                          .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                      },
                              });
        bake_height_atlas(device, gpu);

        const ReferenceGridData grid = make_reference_grid(config_.mesh_cells);
        mesh_.emplace(gpu, cubey::render::indexed_mesh_config(
                               std::span<const ReferenceGridVertex>(grid.vertices),
                               std::span<const std::uint32_t>(grid.indices)));
    }

    void create_frame_resources(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent, std::uint32_t frame_slot_count) {
        if (!height_atlas_.has_value()) {
            throw std::runtime_error("reference height atlas is not initialized");
        }
        depth_attachment_.emplace(device, extent);
        frame_material_.emplace(device,
                                cubey::render::FrameUniformMaterialInstanceConfig{
                                    .material_pass = frame_material_pass_info(),
                                    .descriptor_set = 0,
                                    .frame_slot_count = frame_slot_count,
                                    .uniform_binding = 0,
                                    .sampled_images =
                                        {
                                            {
                                                .binding = 1,
                                                .sampler = height_atlas_->sampler().handle(),
                                                .image_view = height_atlas_->view(),
                                            },
                                            {
                                                .binding = 2,
                                                .sampler = channel_texture().sampler().handle(),
                                                .image_view = channel_texture().view(),
                                            },
                                            {
                                                .binding = 3,
                                                .sampler = channel_texture().sampler().handle(),
                                                .image_view = channel_texture().view(),
                                            },
                                        },
                                });

        const std::array<VkDescriptorSetLayout, 1> layouts{frame_material_->layout()};
        const std::array<cubey::render::ShaderStageFile, 2> sky_shaders{
            cubey::render::vertex_shader_file(shader_path("fullscreen.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("mountains_sky.frag.spv")),
        };
        sky_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                          .extent = extent,
                                          .color_format = color_format,
                                          .depth_format = depth_attachment_->format(),
                                          .shader_stage_files = sky_shaders,
                                          .descriptor_set_layouts = layouts,
                                          .material_pass = sky_pass_info(),
                                      });

        const std::array<cubey::render::ShaderStageFile, 2> diagnostic_shaders{
            cubey::render::vertex_shader_file(shader_path("fullscreen.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("mountains_atlas_diagnostic.frag.spv")),
        };
        diagnostic_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                                 .extent = extent,
                                                 .color_format = color_format,
                                                 .depth_format = depth_attachment_->format(),
                                                 .shader_stage_files = diagnostic_shaders,
                                                 .descriptor_set_layouts = layouts,
                                                 .material_pass = diagnostic_pass_info(),
                                             });

        const std::array<VkVertexInputBindingDescription, 1> bindings{{
            {
                .binding = 0,
                .stride = sizeof(ReferenceGridVertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },
        }};
        const std::array<VkVertexInputAttributeDescription, 1> attributes{{
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = 0,
            },
        }};
        const std::array<cubey::render::ShaderStageFile, 2> mesh_shaders{
            cubey::render::vertex_shader_file(shader_path("mountains_mesh.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("mountains_mesh.frag.spv")),
        };
        mesh_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                           .extent = extent,
                                           .color_format = color_format,
                                           .depth_format = depth_attachment_->format(),
                                           .shader_stage_files = mesh_shaders,
                                           .vertex_bindings = bindings,
                                           .vertex_attributes = attributes,
                                           .descriptor_set_layouts = layouts,
                                           .material_pass = mesh_pass_info(),
                                       });
    }

    void destroy_frame_resources() {
        mesh_pipeline_.reset();
        diagnostic_pipeline_.reset();
        sky_pipeline_.reset();
        frame_material_.reset();
        depth_attachment_.reset();
    }

    void destroy_global_resources() {
        destroy_frame_resources();
        mesh_.reset();
        height_atlas_.reset();
        channel_texture_ = nullptr;
    }

    void record(VkCommandBuffer command_buffer, const cubey::render::ColorTargetView& color_target,
                cubey::render::FrameSlot frame_slot, bool present) {
        frame_material().upload(frame_slot, frame_uniforms(color_target.extent));
        const cubey::render::RenderTargetView target = cubey::render::render_target_view(
            color_target, cubey::render::depth_target_view(depth_attachment()));
        const cubey::render::RenderClearValues clear{
            .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            .depth = cubey::render::depth_clear_value(),
        };
        const auto record_scene = [this,
                                   frame_slot](const cubey::vulkan::CommandRecorder& recorder) {
            if (config_.diagnostic != ReferenceDiagnostic::Final) {
                cubey::render::record_fullscreen_pipeline_draw(
                    recorder, {
                                  .pipeline = &diagnostic_pipeline(),
                                  .descriptor_set = frame_material().set(frame_slot),
                              });
                return;
            }
            cubey::render::record_fullscreen_pipeline_draw(
                recorder, {
                              .pipeline = &sky_pipeline(),
                              .descriptor_set = frame_material().set(frame_slot),
                          });
            recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline().pipeline());
            recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipeline().layout(),
                                         0, frame_material().set(frame_slot));
            cubey::render::record_draw_item(recorder.handle(), {.mesh = &mesh()});
        };

        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        if (present) {
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            cubey::render::record_present_render_target_pass(recorder, target, clear, record_scene);
            recorder.end("vkEndCommandBuffer terrain ShaderToy mesh reference");
        } else {
            recorder.transition_image_layout(
                cubey::vulkan::begin_depth_attachment_transition(depth_attachment().handle()));
            cubey::render::record_render_target_pass(recorder, target, clear, record_scene);
        }
    }

  private:
    [[nodiscard]] ReferenceCamera probe_camera(cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu,
                                               VkExtent2D reference_extent) const {
        constexpr VkExtent2D probe_extent{4U, 1U};
        cubey::vulkan::Image probe_image(device, cubey::vulkan::color_render_target_image_config(
                                                     probe_extent, VK_FORMAT_R32G32B32A32_SFLOAT));
        cubey::vulkan::Buffer readback(
            device, cubey::vulkan::readback_buffer_config(sizeof(cubey::math::Vec4) * 4U));

        const cubey::render::MaterialPassInfo pass = source_fullscreen_pass_info(
            "terrain_shadertoy_ref.camera_probe", sizeof(SourcePushConstants));
        cubey::render::MaterialInstance material(device,
                                                 {.material_pass = pass, .descriptor_set = 0});
        cubey::render::MaterialDescriptorWriter(material.set())
            .combined_image_sampler(0, channel_texture().sampler().handle(),
                                    channel_texture().view())
            .combined_image_sampler(1, channel_texture().sampler().handle(),
                                    channel_texture().view())
            .update(device);
        const std::array<VkDescriptorSetLayout, 1> layouts{material.layout()};
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("fullscreen.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("mountains_camera_probe.frag.spv")),
        };
        cubey::render::GraphicsPipelineResource pipeline(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = probe_extent,
                        .color_format = probe_image.format(),
                        .shader_stage_files = shaders,
                        .descriptor_set_layouts = layouts,
                        .material_pass = pass,
                    });
        const SourcePushConstants constants =
            source_push_constants(reference_extent, config_.reference_time_seconds);
        static_cast<void>(gpu.submit_and_wait({
            .label = "terrain ShaderToy camera probe",
            .work =
                [&probe_image, &readback, &pipeline, &material, probe_extent,
                 constants](cubey::vulkan::GpuOwnerContext& context) {
                    cubey::vulkan::ImmediateCommands commands(context);
                    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                    recorder.transition_image_layout(
                        cubey::vulkan::begin_color_attachment_transition(probe_image.handle()));
                    const cubey::render::ColorTargetView target =
                        cubey::render::color_target_view(probe_extent, probe_image.format(),
                                                         probe_image.handle(), probe_image.view());
                    cubey::render::record_render_target_pass(
                        recorder, cubey::render::render_target_view(target),
                        {.color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 0.0F)},
                        [&pipeline, &material,
                         constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                            cubey::render::record_fullscreen_pipeline_draw(
                                pass_recorder,
                                {
                                    .pipeline = &pipeline,
                                    .descriptor_set = material.set(),
                                },
                                VK_SHADER_STAGE_FRAGMENT_BIT, constants);
                        });
                    recorder.transition_image_layout(
                        cubey::vulkan::finish_color_attachment_for_readback_transition(
                            probe_image.handle()));
                    const VkBufferImageCopy copy = cubey::vulkan::buffer_image_copy(
                        VkExtent3D{probe_extent.width, probe_extent.height, 1U});
                    vkCmdCopyImageToBuffer(commands.command_buffer(), probe_image.handle(),
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle(),
                                           1, &copy);
                    commands.submit_and_wait();
                },
        }));

        std::array<cubey::math::Vec4, 4> values{};
        readback.download(values.data(), sizeof(values));
        return {
            .position = cubey::math::Vec3(values[0]),
            .right = cubey::math::Vec3(values[1]),
            .up = cubey::math::Vec3(values[2]),
            .forward = cubey::math::Vec3(values[3]),
        };
    }

    void bake_height_atlas(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
        const cubey::render::MaterialPassInfo pass = source_fullscreen_pass_info(
            "terrain_shadertoy_ref.height_bake", sizeof(BakePushConstants));
        cubey::render::MaterialInstance material(device,
                                                 {.material_pass = pass, .descriptor_set = 0});
        cubey::render::MaterialDescriptorWriter(material.set())
            .combined_image_sampler(0, channel_texture().sampler().handle(),
                                    channel_texture().view())
            .combined_image_sampler(1, channel_texture().sampler().handle(),
                                    channel_texture().view())
            .update(device);
        const std::array<VkDescriptorSetLayout, 1> layouts{material.layout()};
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("fullscreen.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("mountains_height_bake.frag.spv")),
        };
        cubey::render::GraphicsPipelineResource pipeline(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = height_atlas_->extent(),
                        .color_format = height_atlas_->format(),
                        .shader_stage_files = shaders,
                        .descriptor_set_layouts = layouts,
                        .material_pass = pass,
                    });
        const BakePushConstants constants{
            .resolution_time =
                {
                    static_cast<float>(kHeightAtlasExtent),
                    static_cast<float>(kHeightAtlasExtent),
                    1.0F,
                    config_.reference_time_seconds,
                },
            .mouse = {0.0F, 0.0F, 0.0F, 0.0F},
            .domain_center_extent =
                {
                    camera_.position.x,
                    camera_.position.z,
                    kMeshDomainExtent,
                    0.0F,
                },
        };
        static_cast<void>(gpu.submit_and_wait({
            .label = "terrain ShaderToy height bake",
            .work =
                [this, &pipeline, &material, constants](cubey::vulkan::GpuOwnerContext& context) {
                    cubey::vulkan::ImmediateCommands commands(context);
                    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                    recorder.transition_image_layout(
                        cubey::vulkan::begin_color_attachment_transition(height_atlas_->handle()));
                    const cubey::render::ColorTargetView target = cubey::render::color_target_view(
                        height_atlas_->extent(), height_atlas_->format(), height_atlas_->handle(),
                        height_atlas_->view());
                    cubey::render::record_render_target_pass(
                        recorder, cubey::render::render_target_view(target),
                        {.color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 0.0F)},
                        [&pipeline, &material,
                         constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                            cubey::render::record_fullscreen_pipeline_draw(
                                pass_recorder,
                                {
                                    .pipeline = &pipeline,
                                    .descriptor_set = material.set(),
                                },
                                VK_SHADER_STAGE_FRAGMENT_BIT, constants);
                        });
                    recorder.transition_image_layout(
                        finish_bake_for_sampling(height_atlas_->handle()));
                    commands.submit_and_wait();
                },
        }));
    }

    [[nodiscard]] ReferenceFrameUniforms frame_uniforms(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        return {
            .view_projection = reference_view_projection(camera_, aspect),
            .camera_position_time =
                {
                    camera_.position.x,
                    camera_.position.y,
                    camera_.position.z,
                    config_.reference_time_seconds,
                },
            .camera_right = cubey::math::Vec4(camera_.right, 0.0F),
            .camera_up = cubey::math::Vec4(camera_.up, 0.0F),
            .camera_forward = cubey::math::Vec4(camera_.forward, 0.0F),
            .domain_center_extent_surface =
                {
                    camera_.position.x,
                    camera_.position.z,
                    kMeshDomainExtent,
                    config_.mesh_surface == ReferenceMeshSurface::Map ? 1.0F : 0.0F,
                },
            .resolution_options =
                {
                    static_cast<float>(extent.width),
                    static_cast<float>(extent.height),
                    config_.normal == ReferenceNormal::Detailed ? 1.0F : 0.0F,
                    config_.shading == ReferenceShading::Original ? 1.0F : 0.0F,
                },
            .diagnostic_options =
                {
                    config_.diagnostic == ReferenceDiagnostic::Height ? 1.0F : 2.0F,
                    -10.0F,
                    180.0F,
                    4.0F,
                },
        };
    }

    [[nodiscard]] const cubey::render::Texture2D& channel_texture() const {
        if (channel_texture_ == nullptr) {
            throw std::runtime_error("reference channel texture is not assigned");
        }
        return *channel_texture_;
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<ReferenceFrameUniforms>&
    frame_material() const {
        if (!frame_material_.has_value()) {
            throw std::runtime_error("reference frame material is not initialized");
        }
        return frame_material_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& sky_pipeline() const {
        if (!sky_pipeline_.has_value()) {
            throw std::runtime_error("reference sky pipeline is not initialized");
        }
        return sky_pipeline_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& mesh_pipeline() const {
        if (!mesh_pipeline_.has_value()) {
            throw std::runtime_error("reference mesh pipeline is not initialized");
        }
        return mesh_pipeline_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& diagnostic_pipeline() const {
        if (!diagnostic_pipeline_.has_value()) {
            throw std::runtime_error("reference diagnostic pipeline is not initialized");
        }
        return diagnostic_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::DepthAttachment& depth_attachment() const {
        if (!depth_attachment_.has_value()) {
            throw std::runtime_error("reference depth attachment is not initialized");
        }
        return depth_attachment_.value();
    }

    [[nodiscard]] const cubey::render::Mesh& mesh() const {
        if (!mesh_.has_value()) {
            throw std::runtime_error("reference mesh is not initialized");
        }
        return mesh_.value();
    }

    TerrainShadertoyRefConfig config_{};
    ReferenceCamera camera_{};
    const cubey::render::Texture2D* channel_texture_ = nullptr;
    std::optional<cubey::render::Texture2D> height_atlas_{};
    std::optional<cubey::render::Mesh> mesh_{};
    std::optional<cubey::vulkan::DepthAttachment> depth_attachment_{};
    std::optional<cubey::render::FrameUniformMaterialInstance<ReferenceFrameUniforms>>
        frame_material_{};
    std::optional<cubey::render::GraphicsPipelineResource> sky_pipeline_{};
    std::optional<cubey::render::GraphicsPipelineResource> diagnostic_pipeline_{};
    std::optional<cubey::render::GraphicsPipelineResource> mesh_pipeline_{};
};

MountainsMeshRenderer::MountainsMeshRenderer() : impl_(std::make_unique<Impl>()) {}

MountainsMeshRenderer::~MountainsMeshRenderer() = default;

void MountainsMeshRenderer::create_global_resources(
    cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const TerrainShadertoyRefConfig& config, VkExtent2D reference_extent,
    const cubey::render::Texture2D& channel_texture) {
    impl_->create_global_resources(device, gpu, config, reference_extent, channel_texture);
}

void MountainsMeshRenderer::create_frame_resources(cubey::vulkan::Device& device,
                                                   VkFormat color_format, VkExtent2D extent,
                                                   std::uint32_t frame_slot_count) {
    impl_->create_frame_resources(device, color_format, extent, frame_slot_count);
}

void MountainsMeshRenderer::destroy_frame_resources() {
    impl_->destroy_frame_resources();
}

void MountainsMeshRenderer::destroy_global_resources() {
    impl_->destroy_global_resources();
}

void MountainsMeshRenderer::record(VkCommandBuffer command_buffer,
                                   const cubey::render::ColorTargetView& color_target,
                                   cubey::render::FrameSlot frame_slot, bool present) {
    impl_->record(command_buffer, color_target, frame_slot, present);
}

} // namespace cubey::projects::terrain_shadertoy_ref
