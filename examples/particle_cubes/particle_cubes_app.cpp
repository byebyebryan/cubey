#include "particle_cubes_app.h"

#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/color_space.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_PARTICLE_CUBES_SHADER_DIR
#error "CUBEY_PARTICLE_CUBES_SHADER_DIR must be defined by the particle_cubes CMake target"
#endif

namespace cubey::examples::particle_cubes {
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::vulkan::vk_struct;

constexpr std::uint32_t kParticleCubeCount = 4096;
constexpr std::uint32_t kComputeGroupSize = 128;
constexpr std::uint32_t kCubeTriangleCount = 12;
constexpr float kGoldenAngle = 2.39996314F;
constexpr float kParticleCubeMinScale = 0.01F;
constexpr float kParticleCubeScaleRange = 0.03F;
constexpr float kCameraDistance = 6.4F;
constexpr float kCameraBasePitch = -0.32F;

struct ParticleCubeGpu {
    std::array<float, 4> position_scale{};
    std::array<float, 4> velocity_seed{};
    std::array<float, 4> color{};
};

struct DrawPushConstants {
    cubey::math::Mat4 view_projection;
};

struct ComputePushConstants {
    std::array<float, 4> attractor_dt{};
    std::array<float, 4> bounds_damping_time{};
};

static_assert(sizeof(ParticleCubeGpu) == 48);
static_assert(sizeof(DrawPushConstants) == sizeof(cubey::math::Mat4));
static_assert(sizeof(ComputePushConstants) == 32);

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PARTICLE_CUBES_SHADER_DIR) / filename;
}

cubey::render::MaterialPassInfo particle_cubes_forward_pass_info() {
    const VkPushConstantRange draw_push_constant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(DrawPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "particle_cubes.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .push_constants = {draw_push_constant},
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] float hash01(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

[[nodiscard]] std::vector<ParticleCubeGpu> make_initial_particle_cubes() {
    std::vector<ParticleCubeGpu> cubes;
    cubes.reserve(kParticleCubeCount);

    for (std::uint32_t i = 0; i < kParticleCubeCount; ++i) {
        const float rank =
            (static_cast<float>(i) + 0.5F) / static_cast<float>(kParticleCubeCount);
        const float angle = static_cast<float>(i) * kGoldenAngle;
        const float radius = std::sqrt(rank) * 2.15F;
        const float heat = hash01((i * 277803737U) + 1013904223U);
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        const float y = (hash01((i * 747796405U) + 2891336453U) - 0.5F) * 1.4F;
        const float scale =
            kParticleCubeMinScale +
            (hash01((i * 1597334677U) + 3812015801U) * kParticleCubeScaleRange);
        const cubey::math::Vec4 color = cubey::render::srgb_to_linear_rgba({
            0.38F + (heat * 0.45F),
            0.74F - (heat * 0.22F),
            0.92F - (heat * 0.38F),
            1.0F,
        });

        cubes.push_back({
            .position_scale = {x, y, z, scale},
            .velocity_seed = {-z * 0.055F, 0.0F, x * 0.055F, heat},
            .color = {color.r, color.g, color.b, color.a},
        });
    }

    return cubes;
}

class ParticleCubesApp {
  public:
    explicit ParticleCubesApp(RunConfig config) : config_(std::move(config)) {}

    ParticleCubesApp(const ParticleCubesApp&) = delete;
    ParticleCubesApp& operator=(const ParticleCubesApp&) = delete;

    int run() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context);
            create_forward_pass(context);
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            orbit_controller_.update_from_input(context.input(), timing.delta_seconds);
            if (context.input().key_pressed(cubey::input::Key::Space)) {
                paused_ = !paused_;
            }
            if (context.input().key_pressed(cubey::input::Key::R)) {
                reset_cubes_requested_ = true;
            }
            if (reset_cubes_requested_) {
                reset_particle_buffer(context);
            }
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            (void)context;
            record_particle_cubes_frame(frame);
        };
        callbacks.frame_stats_sample =
            [](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            const VkExtent2D extent = context.swapchain().extent();
            return FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = extent.width,
                .height = extent.height,
                .triangles = kParticleCubeCount * kCubeTriangleCount,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "particle_cubes",
                .ready_status = "rendering compute-driven cube particles",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

  private:
    void create_global_resources_if_needed(cubey::host::WindowedAppContext& context) {
        if (particle_buffer_.has_value()) {
            return;
        }

        create_cube_mesh(context);
        create_particle_buffer(context);
        create_descriptor_resources(context);
        create_compute_resources(context);
    }

    void create_cube_mesh(cubey::host::WindowedAppContext& context) {
        const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>
            mesh_data = cubey::render::make_cube_position_color_normal_mesh({
                .half_extent = 1.0F,
            });
        cube_mesh_.emplace(context.gpu(), mesh_data.mesh_config());
    }

    void create_particle_buffer(cubey::host::WindowedAppContext& context) {
        const std::vector<ParticleCubeGpu> cubes = make_initial_particle_cubes();
        const VkDeviceSize byte_size =
            static_cast<VkDeviceSize>(cubes.size() * sizeof(ParticleCubeGpu));
        particle_buffer_.emplace(cubey::vulkan::upload_device_buffer(
            context.gpu(), cubes.data(), byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    }

    void create_descriptor_resources(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo info(bindings);
        descriptors_.emplace(context.device(), info);
        update_particle_descriptor(context);
    }

    void update_particle_descriptor(cubey::host::WindowedAppContext& context) {
        cubey::vulkan::DescriptorWriteBatch descriptor_writes;
        descriptor_writes.storage_buffer(descriptors().set(), 0, particle_buffer().handle(),
                                         particle_buffer().size());
        descriptor_writes.update(context.device());
    }

    void reset_particle_buffer(cubey::host::WindowedAppContext& context) {
        context.gpu().wait_queue_idle("vkQueueWaitIdle before particle cube reset");
        particle_buffer_.reset();
        create_particle_buffer(context);
        update_particle_descriptor(context);
        reset_cubes_requested_ = false;
    }

    void create_compute_resources(cubey::host::WindowedAppContext& context) {
        const VkPushConstantRange compute_push_constant{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(ComputePushConstants),
        };
        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        const std::array<VkPushConstantRange, 1> push_constants{compute_push_constant};
        compute_pipeline_resource_.emplace(
            context.device(), cubey::render::ComputePipelineResourceConfig{
                                  .shader_stage =
                                      cubey::render::compute_shader_file(
                                          shader_path("particle_cubes.comp.spv")),
                                  .descriptor_set_layouts = set_layouts,
                                  .push_constants = push_constants,
                              });
    }

    void create_forward_pass(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("particle_cubes.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("particle_cubes.frag.spv")),
        };
        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_input_layout();
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
                        .material_pass = particle_cubes_forward_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.012F, 0.015F, 0.02F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });
    }

    void destroy_swapchain_resources() {
        forward_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        compute_pipeline_resource_.reset();
        descriptors_.reset();
        particle_buffer_.reset();
        cube_mesh_.reset();
    }

    void record_particle_compute(const cubey::vulkan::CommandRecorder& recorder,
                                 const FrameTiming& timing) const {
        const float time = static_cast<float>(timing.elapsed_seconds);
        const float delta_seconds =
            std::min(static_cast<float>(timing.delta_seconds), 1.0F / 30.0F);
        const ComputePushConstants push_constants{
            .attractor_dt =
                {
                    std::cos(time * 0.51F) * 1.15F,
                    std::sin(time * 0.73F) * 0.55F,
                    std::sin(time * 0.37F) * 1.10F,
                    delta_seconds,
                },
            .bounds_damping_time =
                {
                    2.75F,
                    0.985F,
                    time,
                    0.0F,
                },
        };

        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                               compute_pipeline_resource().pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                     compute_pipeline_resource().layout(), 0, descriptors().set());
        recorder.push_constants(compute_pipeline_resource().layout(), VK_SHADER_STAGE_COMPUTE_BIT,
                                0, push_constants);
        recorder.dispatch((kParticleCubeCount + kComputeGroupSize - 1U) / kComputeGroupSize, 1, 1);

        const std::array<VkMemoryBarrier, 1> particle_barriers{{
            [&] {
                auto barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                return barrier;
            }(),
        }};
        recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, particle_barriers);
    }

    [[nodiscard]] DrawPushConstants draw_push_constants(const FrameTiming& timing,
                                                        VkExtent2D extent) const {
        (void)timing;
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
            .distance = kCameraDistance,
            .yaw = orbit_controller_.yaw(),
            .pitch = kCameraBasePitch + orbit_controller_.pitch(),
        });
        const cubey::Camera3D camera;
        return {
            .view_projection = camera.view_projection_matrix(camera_transform, aspect),
        };
    }

    void record_particle_cubes_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        if (!paused_) {
            record_particle_compute(recorder, frame.timing);
        }

        forward_pass().record_to_present(
            recorder, frame.color_target,
            [this, &frame](const cubey::vulkan::CommandRecorder& pass_recorder) {
                const cubey::render::GraphicsPipelineResource& pipeline =
                    forward_pass().pipeline();
                pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
                pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                  pipeline.layout(), 0, descriptors().set());
                pass_recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                             draw_push_constants(frame.timing,
                                                                 frame.color_target.extent));
                cubey::render::record_draw_item(pass_recorder.handle(),
                                                {
                                                    .mesh = &cube_mesh(),
                                                    .instance_count = kParticleCubeCount,
                                                });
            });

        recorder.end("vkEndCommandBuffer particle_cubes");
    }

    [[nodiscard]] const cubey::render::Mesh& cube_mesh() const {
        if (!cube_mesh_.has_value()) {
            throw std::runtime_error("particle cube mesh is not initialized");
        }
        return cube_mesh_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& descriptors() const {
        if (!descriptors_.has_value()) {
            throw std::runtime_error("particle cube descriptors are not initialized");
        }
        return descriptors_.value();
    }

    [[nodiscard]] const cubey::vulkan::Buffer& particle_buffer() const {
        if (!particle_buffer_.has_value()) {
            throw std::runtime_error("particle cube buffer is not initialized");
        }
        return particle_buffer_.value();
    }

    [[nodiscard]] const cubey::render::ComputePipelineResource& compute_pipeline_resource() const {
        if (!compute_pipeline_resource_.has_value()) {
            throw std::runtime_error("particle cube compute pipeline is not initialized");
        }
        return compute_pipeline_resource_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("particle cube forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    OrbitController orbit_controller_;
    bool paused_ = false;
    bool reset_cubes_requested_ = false;

    std::optional<cubey::render::Mesh> cube_mesh_;
    std::optional<cubey::vulkan::Buffer> particle_buffer_;
    std::optional<cubey::vulkan::DescriptorSetBundle> descriptors_;
    std::optional<cubey::render::ComputePipelineResource> compute_pipeline_resource_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
};

} // namespace

int run_particle_cubes(const RunConfig& config) {
    ParticleCubesApp app(config);
    return app.run();
}

} // namespace cubey::examples::particle_cubes
