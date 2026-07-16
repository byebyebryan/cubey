#include "terrain_shadertoy_ref_app.h"
#include "terrain_shadertoy_ref_mesh.h"

#include <cubey/core/profiling.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
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

constexpr std::uint32_t kChannelTextureExtent = 256U;
constexpr std::uint32_t kReferenceGpuProfilerPassCapacity = 3U;

[[nodiscard]] std::uint64_t profile_frame_index(std::uint64_t frame_index) {
    return frame_index == 0U ? 0U : frame_index - 1U;
}

[[nodiscard]] std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                                          cubey::render::FrameSlot frame_slot) {
    if (frame_index > frame_slot.count) {
        return frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame_index);
}

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings) {
    if (recorder == nullptr) {
        return;
    }
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        recorder->record_gpu_span(frame_index, timing.label, timing.milliseconds);
    }
}

struct RaymarchPushConstants {
    std::array<float, 4> resolution_time{};
    std::array<float, 4> mouse{};
};

static_assert(sizeof(RaymarchPushConstants) == 32U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TERRAIN_SHADERTOY_REF_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo raymarch_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(RaymarchPushConstants),
    };
    return {
        .label = "terrain_shadertoy_ref.raymarch",
        .descriptor_sets = {cubey::render::sampled_texture_descriptor_set_layout(0, 2)},
        .push_constants = {push_constant_range},
    };
}

[[nodiscard]] std::vector<std::uint8_t> make_channel_texture_bytes() {
    std::vector<std::uint8_t> bytes(kChannelTextureExtent * kChannelTextureExtent * 4U);
    for (std::uint32_t y = 0; y < kChannelTextureExtent; ++y) {
        for (std::uint32_t x = 0; x < kChannelTextureExtent; ++x) {
            std::uint32_t hash = x * 0x9e3779b9U + y * 0x85ebca6bU + 0x51ed270bU;
            hash ^= hash >> 16U;
            hash *= 0x7feb352dU;
            hash ^= hash >> 15U;
            const std::size_t offset = static_cast<std::size_t>(y * kChannelTextureExtent + x) * 4U;
            bytes[offset + 0U] = static_cast<std::uint8_t>(hash & 0xffU);
            bytes[offset + 1U] = static_cast<std::uint8_t>((hash >> 8U) & 0xffU);
            bytes[offset + 2U] = static_cast<std::uint8_t>((hash >> 16U) & 0xffU);
            bytes[offset + 3U] = 255U;
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> make_control_texture_bytes() {
    return std::vector<std::uint8_t>(kChannelTextureExtent * kChannelTextureExtent * 4U, 0U);
}

class TerrainShadertoyRefApp {
  public:
    TerrainShadertoyRefApp(RunConfig run_config, TerrainShadertoyRefConfig reference_config)
        : run_config_(std::move(run_config)), reference_config_(reference_config) {}

    TerrainShadertoyRefApp(const TerrainShadertoyRefApp&) = delete;
    TerrainShadertoyRefApp& operator=(const TerrainShadertoyRefApp&) = delete;

    ~TerrainShadertoyRefApp() {
        destroy_swapchain_resources();
        destroy_global_resources();
    }

    int run() {
        if (reference_config_.render == ReferenceRender::Raymarch &&
            reference_config_.diagnostic != ReferenceDiagnostic::Final) {
            throw std::runtime_error("reference diagnostics require --reference-render mesh");
        }
        return run_config_.headless ? run_headless() : run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources(context.device(), context.gpu(), context.swapchain().extent(),
                                    context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            if (reference_config_.render != ReferenceRender::Mesh) {
                return;
            }
            orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
            mesh_renderer_.set_inspection_orbit(orbit_controller_.yaw(), orbit_controller_.pitch(),
                                                orbit_controller_.distance());
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context, frame);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
            destroy_global_resources();
        };

        const std::string ready_status =
            reference_config_.render == ReferenceRender::Mesh
                ? "rendering external terrain source study " +
                      std::string(reference_study_name(reference_config_.study))
                : "rendering external Mountains raymarch";

        return cubey::host::run_windowed_app(
            {
                .run_config = run_config_,
                .app_name = "terrain_shadertoy_ref",
                .ready_status = ready_status.c_str(),
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = run_config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_global_resources(context.device(), context.gpu(), target.extent,
                                    cubey::host::headless_capture_frame_slot_count(run_config_));
            create_pipeline(context.device(), target.format, target.extent,
                            cubey::host::headless_capture_frame_slot_count(run_config_));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            collect_gpu_timings(context.profile_recorder(), frame.timing.frame_index,
                                frame.frame_slot);
            if (reference_config_.render == ReferenceRender::Mesh) {
                mesh_renderer_.record(command_buffer, target, frame.frame_slot, false,
                                      gpu_profiler());
            } else {
                if (cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler()) {
                    profiler->begin_frame(command_buffer, frame.frame_slot.index);
                }
                record_reference_draw(cubey::vulkan::CommandRecorder(command_buffer), target,
                                      frame.frame_slot);
            }
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
            destroy_global_resources();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void create_global_resources(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                                 VkExtent2D reference_extent, std::uint32_t frame_slot_count) {
        gpu_profiler_.emplace(device, frame_slot_count, kReferenceGpuProfilerPassCapacity);
        const std::vector<std::uint8_t> bytes = make_channel_texture_bytes();
        channel_texture_.emplace(cubey::render::create_uploaded_texture_2d(
            device, gpu,
            {
                .extent = {kChannelTextureExtent, kChannelTextureExtent},
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .rgba8 = std::span<const std::uint8_t>(bytes.data(), bytes.size()),
                .create_sampler = true,
                .sampler =
                    {
                        .min_filter = VK_FILTER_LINEAR,
                        .mag_filter = VK_FILTER_LINEAR,
                        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    },
            }));
        const std::vector<std::uint8_t> control_bytes = make_control_texture_bytes();
        control_texture_.emplace(cubey::render::create_uploaded_texture_2d(
            device, gpu,
            {
                .extent = {kChannelTextureExtent, kChannelTextureExtent},
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .rgba8 = std::span<const std::uint8_t>(control_bytes.data(), control_bytes.size()),
                .create_sampler = true,
                .sampler =
                    {
                        .min_filter = VK_FILTER_NEAREST,
                        .mag_filter = VK_FILTER_NEAREST,
                        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    },
            }));
        if (reference_config_.render == ReferenceRender::Mesh) {
            mesh_renderer_.create_global_resources(device, gpu, reference_config_, reference_extent,
                                                   channel_texture(), control_texture());
            const float focus_distance = mesh_renderer_.inspection_focus_distance();
            orbit_controller_.set_distance_limits(std::max(focus_distance * 0.1F, 2.0F),
                                                  std::max(focus_distance * 8.0F, 512.0F));
            orbit_controller_.set_home_distance(focus_distance);
            orbit_controller_.set_pitch_limits(-1.45F, 1.45F);
            orbit_controller_.reset();
        }
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count) {
        if (reference_config_.render == ReferenceRender::Mesh) {
            mesh_renderer_.create_frame_resources(device, color_format, extent, frame_slot_count);
            return;
        }
        const cubey::render::MaterialPassInfo pass = raymarch_pass_info();
        material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                      .material_pass = pass,
                                      .descriptor_set = 0,
                                  });
        cubey::render::MaterialDescriptorWriter(material_->set())
            .combined_image_sampler(0, channel_texture().sampler().handle(),
                                    channel_texture().view())
            .combined_image_sampler(1, channel_texture().sampler().handle(),
                                    channel_texture().view())
            .update(device);

        const std::array<VkDescriptorSetLayout, 1> layouts{material_->layout()};
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("fullscreen.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("mountains_raymarch.frag.spv")),
        };
        pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                      .extent = extent,
                                      .color_format = color_format,
                                      .shader_stage_files = shaders,
                                      .descriptor_set_layouts = layouts,
                                      .material_pass = pass,
                                  });
    }

    void destroy_swapchain_resources() {
        mesh_renderer_.destroy_frame_resources();
        pipeline_.reset();
        material_.reset();
    }

    void destroy_global_resources() {
        mesh_renderer_.destroy_global_resources();
        control_texture_.reset();
        channel_texture_.reset();
        gpu_profiler_.reset();
    }

    [[nodiscard]] RaymarchPushConstants push_constants(VkExtent2D extent) const {
        return {
            .resolution_time =
                {
                    static_cast<float>(extent.width),
                    static_cast<float>(extent.height),
                    1.0F,
                    reference_config_.reference_time_seconds,
                },
            .mouse = {0.0F, 0.0F, 0.0F, 0.0F},
        };
    }

    void record_reference_draw(const cubey::vulkan::CommandRecorder& recorder,
                               const cubey::render::ColorTargetView& target,
                               cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampScope profile_scope(
            gpu_profiler(), recorder.handle(), frame_slot.index,
            "terrain_shadertoy_ref.raymarch");
        const RaymarchPushConstants constants = push_constants(target.extent);
        cubey::render::record_render_target_pass(
            recorder, cubey::render::render_target_view(target),
            {.color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F)},
            [this, constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder,
                    {
                        .pipeline = &pipeline(),
                        .descriptor_set = material().set(),
                    },
                    VK_SHADER_STAGE_FRAGMENT_BIT, constants);
            });
    }

    void record_windowed_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& frame) {
        collect_gpu_timings(context.profile_recorder(), frame.timing.frame_index,
                            frame.frame_slot);
        if (reference_config_.render == ReferenceRender::Mesh) {
            mesh_renderer_.record(frame.command_buffer, frame.color_target, frame.frame_slot, true,
                                  gpu_profiler());
            return;
        }
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler()) {
            profiler->begin_frame(frame.command_buffer, frame.frame_slot.index);
        }
        cubey::render::record_present_render_target(
            recorder, cubey::render::render_target_view(frame.color_target),
            [this, &frame](const cubey::vulkan::CommandRecorder& present_recorder) {
                record_reference_draw(present_recorder, frame.color_target, frame.frame_slot);
            });
        recorder.end("vkEndCommandBuffer terrain ShaderToy reference");
    }

    [[nodiscard]] const cubey::render::Texture2D& channel_texture() const {
        if (!channel_texture_.has_value()) {
            throw std::runtime_error("reference channel texture is not initialized");
        }
        return channel_texture_.value();
    }

    [[nodiscard]] const cubey::render::Texture2D& control_texture() const {
        if (!control_texture_.has_value()) {
            throw std::runtime_error("reference control texture is not initialized");
        }
        return control_texture_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& material() const {
        if (!material_.has_value()) {
            throw std::runtime_error("reference material is not initialized");
        }
        return material_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline() const {
        if (!pipeline_.has_value()) {
            throw std::runtime_error("reference pipeline is not initialized");
        }
        return pipeline_.value();
    }

    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* gpu_profiler() {
        return gpu_profiler_.has_value() ? &gpu_profiler_.value() : nullptr;
    }

    void collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                             std::uint64_t frame_index, cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler();
        if (profiler == nullptr) {
            return;
        }
        profiler->collect(frame_slot.index);
        record_gpu_timings(profile_recorder, collected_profile_frame_index(frame_index, frame_slot),
                           profiler->latest_timings());
    }

    RunConfig run_config_;
    TerrainShadertoyRefConfig reference_config_;
    std::optional<cubey::render::Texture2D> channel_texture_{};
    std::optional<cubey::render::Texture2D> control_texture_{};
    std::optional<cubey::render::MaterialInstance> material_{};
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_{};
    std::optional<cubey::vulkan::GpuTimestampProfiler> gpu_profiler_{};
    cubey::OrbitController orbit_controller_{};
    ReferenceMeshRenderer mesh_renderer_{};
};

} // namespace

int run_terrain_shadertoy_ref(const RunConfig& run_config,
                              const TerrainShadertoyRefConfig& reference_config) {
    TerrainShadertoyRefApp app(run_config, reference_config);
    return app.run();
}

} // namespace cubey::projects::terrain_shadertoy_ref
