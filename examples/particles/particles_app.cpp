#include "particles_app.h"

#include <cubey/host/frame_stats.h>
#include <cubey/host/glfw_window.h>
#include <cubey/host/windowed_host.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_PARTICLES_SHADER_DIR
#error "CUBEY_PARTICLES_SHADER_DIR must be defined by the particles CMake target"
#endif

namespace cubey::examples::particles {
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::vulkan::vk_struct;

constexpr std::uint32_t kParticleCount = 8192;
constexpr std::uint32_t kComputeGroupSize = 128;
constexpr float kGoldenAngle = 2.39996314F;

struct ParticleGpu {
    std::array<float, 4> position_radius{};
    std::array<float, 4> velocity_seed{};
    std::array<float, 4> color{};
};

struct DrawPushConstants {
    std::array<float, 4> inv_extent_scale_time{};
};

struct ComputePushConstants {
    std::array<float, 4> attractor_strength_dt{};
    std::array<float, 4> bounds_damping_time{};
};

static_assert(sizeof(ParticleGpu) == 48);
static_assert(sizeof(DrawPushConstants) == 16);
static_assert(sizeof(ComputePushConstants) == 32);

[[nodiscard]] cubey::render::MaterialPassInfo particles_draw_pass_info() {
    const VkPushConstantRange draw_push_constant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(DrawPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "particles.draw",
        .push_constants = {draw_push_constant},
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    };
}

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PARTICLES_SHADER_DIR) / filename;
}

[[nodiscard]] float hash01(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

[[nodiscard]] std::vector<ParticleGpu> make_initial_particles() {
    std::vector<ParticleGpu> particles;
    particles.reserve(kParticleCount);

    for (std::uint32_t i = 0; i < kParticleCount; ++i) {
        const float rank = (static_cast<float>(i) + 0.5F) / static_cast<float>(kParticleCount);
        const float angle = static_cast<float>(i) * kGoldenAngle;
        const float radius = std::sqrt(rank) * 0.84F;
        const float jitter = (hash01((i * 747796405U) + 2891336453U) - 0.5F) * 0.08F;
        const float x = std::cos(angle) * (radius + jitter);
        const float y = std::sin(angle) * (radius + jitter);
        const float heat = hash01((i * 277803737U) + 1013904223U);
        const float size_pixels = 2.0F + (hash01((i * 1597334677U) + 3812015801U) * 4.5F);

        particles.push_back({
            .position_radius = {x, y, 0.0F, size_pixels},
            .velocity_seed = {-y * 0.08F, x * 0.08F, 0.0F, heat},
            .color =
                {
                    0.25F + (heat * 0.70F),
                    0.55F + ((1.0F - heat) * 0.25F),
                    0.95F - (heat * 0.40F),
                    0.24F,
                },
        });
    }

    return particles;
}

class ParticlesApp {
  public:
    explicit ParticlesApp(RunConfig config) : config_(std::move(config)) {}

    ParticlesApp(const ParticlesApp&) = delete;
    ParticlesApp& operator=(const ParticlesApp&) = delete;

    int run() {
        if (config_.headless) {
            throw std::runtime_error("particles does not support --headless yet");
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
                        create_pipeline(context);
                    },
                .destroy_swapchain_resources =
                    [this](cubey::host::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
                .on_ready =
                    [](cubey::host::WindowedAppContext& context) {
                        std::printf(
                            "particles: %s rendering compute attractor particles at %ux%u\n",
                            context.device().device_name(), context.swapchain().extent().width,
                            context.swapchain().extent().height);
                    },
                .update =
                    [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
                        (void)timing;
                        if (context.input().key_pressed(cubey::input::Key::Escape)) {
                            context.window().request_close();
                        }
                        if (context.input().key_pressed(cubey::input::Key::Space)) {
                            paused_ = !paused_;
                        }
                        if (context.input().key_pressed(cubey::input::Key::R)) {
                            reset_particles_requested_ = true;
                        }
                        if (reset_particles_requested_) {
                            reset_particle_buffer(context);
                        }
                    },
                .record_frame =
                    [this](cubey::host::WindowedAppContext& context,
                           const cubey::host::WindowedRenderFrame& frame) {
                        (void)context;
                        record_particles_frame(frame);
                    },
                .frame_stats_sample =
                    [](cubey::host::WindowedAppContext& context,
                       const FrameTiming& timing) -> std::optional<FrameStatsSample> {
                    const VkExtent2D extent = context.swapchain().extent();
                    return FrameStatsSample{
                        .delta_seconds = timing.delta_seconds,
                        .width = extent.width,
                        .height = extent.height,
                        .triangles = kParticleCount * 2U,
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
        if (particle_buffer_.has_value()) {
            return;
        }

        create_particle_buffer(context);
        create_descriptor_resources(context);
        create_compute_resources(context);
    }

    void create_particle_buffer(cubey::host::WindowedAppContext& context) {
        const std::vector<ParticleGpu> particles = make_initial_particles();
        const VkDeviceSize byte_size =
            static_cast<VkDeviceSize>(particles.size() * sizeof(ParticleGpu));
        particle_buffer_.emplace(cubey::vulkan::upload_device_buffer(
            context.gpu(), particles.data(), byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
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
        context.gpu().wait_queue_idle("vkQueueWaitIdle before particle reset");
        particle_buffer_.reset();
        create_particle_buffer(context);
        update_particle_descriptor(context);
        reset_particles_requested_ = false;
    }

    void create_compute_resources(cubey::host::WindowedAppContext& context) {
        VkPushConstantRange compute_push_constant{};
        compute_push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        compute_push_constant.offset = 0;
        compute_push_constant.size = sizeof(ComputePushConstants);
        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        const std::array<VkPushConstantRange, 1> push_constants{compute_push_constant};
        compute_pipeline_resource_.emplace(context.device(),
                                           cubey::render::ComputePipelineResourceConfig{
                                               .shader_stage =
                                                   {
                                                       .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                       .path = shader_path("particles.comp.spv"),
                                                   },
                                               .descriptor_set_layouts = set_layouts,
                                               .push_constants = push_constants,
                                           });
    }

    void destroy_swapchain_resources() {
        pipeline_resource_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        compute_pipeline_resource_.reset();
        descriptors_.reset();
        particle_buffer_.reset();
    }

    void create_pipeline(cubey::host::WindowedAppContext& context) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("particles.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("particles.frag.spv"),
            },
        };
        const cubey::render::ShaderProgram shader_program(context.device(), shader_stage_files);

        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
        const cubey::render::MaterialPassInfo material_pass = particles_draw_pass_info();
        pipeline_resource_.emplace(context.device(),
                                   cubey::render::GraphicsPipelineResourceConfig{
                                       .extent = context.swapchain().extent(),
                                       .color_format = context.swapchain().format(),
                                       .shader_stages = shader_program.stages(),
                                       .descriptor_set_layouts = set_layouts,
                                       .material_pass = material_pass,
                                   });
    }

    void record_particle_compute(VkCommandBuffer command_buffer, const FrameTiming& timing) const {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        const float time = static_cast<float>(timing.elapsed_seconds);
        const float delta_seconds =
            std::min(static_cast<float>(timing.delta_seconds), 1.0F / 30.0F);
        const ComputePushConstants push_constants{
            .attractor_strength_dt =
                {
                    std::cos(time * 0.63F) * 0.42F,
                    std::sin(time * 0.97F) * 0.32F,
                    0.32F,
                    delta_seconds,
                },
            .bounds_damping_time =
                {
                    1.08F,
                    0.985F,
                    time,
                    0.0F,
                },
        };

        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                               compute_pipeline_resource().pipeline());
        const VkDescriptorSet descriptor_set = descriptors().set();
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                     compute_pipeline_resource().layout(), 0, descriptor_set);
        recorder.push_constants(compute_pipeline_resource().layout(), VK_SHADER_STAGE_COMPUTE_BIT,
                                0, push_constants);
        recorder.dispatch((kParticleCount + kComputeGroupSize - 1U) / kComputeGroupSize, 1, 1);

        auto particle_barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        particle_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        particle_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &particle_barrier, 0,
                             nullptr, 0, nullptr);
    }

    void record_particles_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        if (!paused_) {
            record_particle_compute(recorder.handle(), frame.timing);
        }

        const VkExtent2D extent = frame.color_target.extent;
        const DrawPushConstants push_constants{
            .inv_extent_scale_time =
                {
                    1.0F / static_cast<float>(extent.width),
                    1.0F / static_cast<float>(extent.height),
                    1.0F,
                    static_cast<float>(frame.timing.elapsed_seconds),
                },
        };

        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(frame.color_target);
        cubey::render::record_present_render_target_pass(
            recorder, target,
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.006F, 0.007F, 0.012F, 1.0F),
            },
            [this, push_constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                const cubey::render::GraphicsPipelineResource& pipeline = pipeline_resource();
                pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
                const VkDescriptorSet descriptor_set = descriptors().set();
                pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                  pipeline.layout(), 0, descriptor_set);
                pass_recorder.push_constants(
                    pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                    push_constants);
                pass_recorder.draw(6, kParticleCount);
            });

        recorder.end("vkEndCommandBuffer particles");
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& descriptors() const {
        if (!descriptors_.has_value()) {
            throw std::runtime_error("particle descriptors are not initialized");
        }
        return descriptors_.value();
    }

    [[nodiscard]] const cubey::vulkan::Buffer& particle_buffer() const {
        if (!particle_buffer_.has_value()) {
            throw std::runtime_error("particle buffer is not initialized");
        }
        return particle_buffer_.value();
    }

    [[nodiscard]] const cubey::render::ComputePipelineResource& compute_pipeline_resource() const {
        if (!compute_pipeline_resource_.has_value()) {
            throw std::runtime_error("compute pipeline resource is not initialized");
        }
        return compute_pipeline_resource_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline_resource() const {
        if (!pipeline_resource_.has_value()) {
            throw std::runtime_error("pipeline resource is not initialized");
        }
        return pipeline_resource_.value();
    }

    RunConfig config_;
    bool paused_ = false;
    bool reset_particles_requested_ = false;

    std::optional<cubey::vulkan::Buffer> particle_buffer_;
    std::optional<cubey::vulkan::DescriptorSetBundle> descriptors_;

    std::optional<cubey::render::GraphicsPipelineResource> pipeline_resource_;
    std::optional<cubey::render::ComputePipelineResource> compute_pipeline_resource_;
};

} // namespace

int run_particles(const RunConfig& config) {
    ParticlesApp app(config);
    return app.run();
}

} // namespace cubey::examples::particles
