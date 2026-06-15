#include "cloud_ref_2_app.h"

#include "cloud_ref_2_config.h"

#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/sampler.h>

#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#ifndef CUBEY_CLOUD_REF_2_SHADER_DIR
#error "CUBEY_CLOUD_REF_2_SHADER_DIR must be defined by the cloud_ref_2 CMake target"
#endif

namespace cubey::projects::cloud_ref_2 {
namespace {

using cubey::FrameTiming;

constexpr float kDefaultFovyRadians = 62.0F * (glm::pi<float>() / 180.0F);
constexpr float kCameraDragRadiansPerPixel = 0.006F;
constexpr float kSurfaceMinPitchRadians = -1.50F;
constexpr float kSurfaceMaxPitchRadians = 1.35F;
constexpr std::uint32_t kBaseNoiseSize = 128U;
constexpr std::uint32_t kDetailNoiseSize = 32U;
constexpr std::uint32_t kWeatherTextureSize = 1024U;
constexpr std::uint32_t kCloudRef2ComputeGroupSize = 16U;
constexpr std::uint32_t kCloudRef2CacheGroupSize = 8U;
constexpr std::uint32_t kCloudRef2VolumeGroupSize = 4U;
constexpr std::uint32_t kCloudRef2CacheTextureCount = 3U;
constexpr VkFormat kCloudRef2ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kCloudRef2NoiseFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kCloudRef2UniformBinding = 0;
constexpr std::uint32_t kCloudRef2OutputBinding = 1;
constexpr std::uint32_t kCloudRef2BaseNoiseBinding = 2;
constexpr std::uint32_t kCloudRef2DetailNoiseBinding = 3;
constexpr std::uint32_t kCloudRef2WeatherBinding = 4;
constexpr std::uint32_t kCloudRef2CompositeBlendFromBinding = 1;
constexpr std::uint32_t kCloudRef2CompositeBlendToBinding = 2;

constexpr std::array<CloudsCameraMode, 6> kCloudRef2CameraModes{
    CloudsCameraMode::Surface, CloudsCameraMode::SurfaceUp, CloudsCameraMode::High,
    CloudsCameraMode::HighOblique, CloudsCameraMode::Orbit, CloudsCameraMode::OrbitTerminator,
};
constexpr std::array<CloudsQuality, 3> kCloudRef2QualityModes{
    CloudsQuality::Quarter,
    CloudsQuality::Half,
    CloudsQuality::Full,
};
constexpr std::array<CloudsWeatherPreset, 5> kCloudRef2WeatherPresets{
    CloudsWeatherPreset::FairWeather,
    CloudsWeatherPreset::BrokenCumulus,
    CloudsWeatherPreset::OvercastStratus,
    CloudsWeatherPreset::StormCells,
    CloudsWeatherPreset::HighCirrus,
};
constexpr std::array<CloudsDebugView, 21> kCloudRef2DebugViews{
    CloudsDebugView::Final,        CloudsDebugView::RawFinal,
    CloudsDebugView::RawCloudProduct,
    CloudsDebugView::Weather,
    CloudsDebugView::Density,      CloudsDebugView::Transmittance,
    CloudsDebugView::Lighting,     CloudsDebugView::AmbientLight,
    CloudsDebugView::DirectLight,  CloudsDebugView::PhaseLight,
    CloudsDebugView::Shadow,       CloudsDebugView::Steps,
    CloudsDebugView::BlendFrom,    CloudsDebugView::BlendTo,
    CloudsDebugView::UpdateRegion, CloudsDebugView::OctUv,
    CloudsDebugView::Background,   CloudsDebugView::CloudAlpha,
    CloudsDebugView::Distance,
    CloudsDebugView::BaseDensity,  CloudsDebugView::DetailDensity,
};
constexpr std::array<CloudsCacheFrames, 4> kCloudRef2CacheFrameModes{
    CloudsCacheFrames::Frames4,
    CloudsCacheFrames::Frames16,
    CloudsCacheFrames::Frames64,
    CloudsCacheFrames::Frames256,
};
constexpr std::array<std::uint32_t, 4> kCloudRef2CacheTextureSizes{256U, 512U, 768U, 1024U};

struct CloudRef2FrameUniforms {
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward_mode;
    cubey::math::Vec4 camera_position_radius;
    cubey::math::Vec4 cloud_shell;
    cubey::math::Vec4 weather;
    cubey::math::Vec4 sun_direction_intensity;
    cubey::math::Vec4 ref_options;
    cubey::math::Vec4 shape_options;
    cubey::math::Vec4 weather_feature_weights;
    cubey::math::Vec4 cloud_color_top_shadow;
    cubey::math::Vec4 cloud_color_bottom_horizon;
    cubey::math::Vec4 cache_status;
    cubey::math::Vec4 cache_region;
};

static_assert(sizeof(CloudRef2FrameUniforms) == sizeof(float) * 56U);

struct CloudRef2CachePushConstants {
    cubey::math::Vec4 update_region;
    cubey::math::Vec4 cache_options;
};

static_assert(sizeof(CloudRef2CachePushConstants) == sizeof(float) * 8U);

struct CloudRef2ViewBasis {
    cubey::math::Vec3 position{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
};

struct CloudRef2CacheUniformState {
    float blend_amount = 1.0F;
    std::uint32_t texture_size = 768U;
    std::uint32_t update_region_size = 96U;
    std::uint32_t update_x = 0U;
    std::uint32_t update_y = 0U;
    std::uint32_t frames_to_update = 64U;
};

enum class CloudRef2TargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

struct CloudRef2FrameGraph {
    cubey::render::CompiledRenderGraph graph{};
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_CLOUD_REF_2_SHADER_DIR) / filename;
}

[[nodiscard]] std::uint32_t cloud_ref_2_mip_count(std::uint32_t size) {
    std::uint32_t levels = 1;
    while (size > 1U) {
        size = std::max(1U, size / 2U);
        ++levels;
    }
    return levels;
}

[[nodiscard]] cubey::vulkan::SamplerConfig cloud_ref_2_repeat_sampler_config(
    std::uint32_t mip_levels = 1) {
    return {
        .min_filter = VK_FILTER_LINEAR,
        .mag_filter = VK_FILTER_LINEAR,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .min_lod = 0.0F,
        .max_lod = static_cast<float>(std::max(1U, mip_levels) - 1U),
    };
}

[[nodiscard]] cubey::render::MaterialDescriptorSetLayout cloud_ref_2_cache_set_layout() {
    return {
        .set = 0,
        .bindings =
            {
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2UniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2OutputBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2BaseNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2DetailNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2WeatherBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
            },
    };
}

[[nodiscard]] cubey::render::MaterialDescriptorSetLayout cloud_ref_2_composite_set_layout() {
    return {
        .set = 0,
        .bindings =
            {
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2UniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2CompositeBlendFromBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudRef2CompositeBlendToBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            },
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo cloud_ref_2_cache_pass_info() {
    return {
        .label = "cloud_ref_2_cache",
        .descriptor_sets = {cloud_ref_2_cache_set_layout()},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo cloud_ref_2_composite_pass_info() {
    return {
        .label = "cloud_ref_2_composite",
        .descriptor_sets = {cloud_ref_2_composite_set_layout()},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = false,
        .depth_write = false,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState
cloud_ref_2_target_initial_state(CloudRef2TargetMode mode) {
    return mode == CloudRef2TargetMode::Present
               ? cubey::render::render_graph_undefined_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] cubey::render::RenderGraphTextureState
cloud_ref_2_target_final_state(CloudRef2TargetMode mode) {
    return mode == CloudRef2TargetMode::Present
               ? cubey::render::render_graph_present_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] float cloud_ref_2_camera_base_pitch(CloudsCameraMode mode) {
    switch (mode) {
    case CloudsCameraMode::SurfaceUp:
        return 0.45F;
    case CloudsCameraMode::High:
        return -0.18F;
    case CloudsCameraMode::HighOblique:
        return -0.42F;
    case CloudsCameraMode::Orbit:
    case CloudsCameraMode::OrbitTerminator:
        return -1.08F;
    case CloudsCameraMode::Surface:
    default:
        return -0.06F;
    }
}

[[nodiscard]] float cloud_ref_2_clamp_pitch(CloudsCameraMode mode, float pitch_offset) {
    const float base_pitch = cloud_ref_2_camera_base_pitch(mode);
    return std::clamp(base_pitch + pitch_offset, kSurfaceMinPitchRadians, kSurfaceMaxPitchRadians) -
           base_pitch;
}

[[nodiscard]] float cloud_ref_2_camera_shader_mode(CloudsCameraMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float cloud_ref_2_cloud_style_value(CloudsCloudStyle style) {
    return static_cast<float>(static_cast<std::uint32_t>(style));
}

[[nodiscard]] CloudRef2ViewBasis cloud_ref_2_view_basis(const CloudsConfig& config, float yaw,
                                                     float pitch_offset) {
    const cubey::math::Vec3 surface_up{0.0F, 1.0F, 0.0F};
    const float yaw_sin = std::sin(yaw);
    const float yaw_cos = std::cos(yaw);
    const cubey::math::Vec3 flat_forward{yaw_sin, 0.0F, -yaw_cos};
    const cubey::math::Vec3 right{yaw_cos, 0.0F, yaw_sin};
    const float pitch = cloud_ref_2_camera_base_pitch(config.camera_mode) + pitch_offset;
    const cubey::math::Vec3 forward =
        glm::normalize(flat_forward * std::cos(pitch) + surface_up * std::sin(pitch));
    return {
        .position = {0.0F, config.camera_altitude_m, 0.0F},
        .right = right,
        .up = glm::normalize(glm::cross(right, forward)),
        .forward = forward,
    };
}

[[nodiscard]] cubey::math::Vec3 cloud_ref_2_source_sun_direction() {
    return glm::normalize(cubey::math::Vec3{-0.5F, 0.5F, 1.0F});
}

[[nodiscard]] CloudRef2FrameUniforms cloud_ref_2_frame_uniforms(const CloudsConfig& config,
                                                            VkExtent2D target_extent,
                                                            float yaw, float pitch,
                                                            float elapsed_seconds,
                                                            CloudRef2CacheUniformState cache) {
    const CloudRef2ViewBasis basis = cloud_ref_2_view_basis(config, yaw, pitch);
    const float aspect =
        static_cast<float>(target_extent.width) / static_cast<float>(target_extent.height);
    const float tan_half_fovy = std::tan(kDefaultFovyRadians * 0.5F);
    const CloudsQualityBudget budget = clouds_quality_budget(config.quality);
    const cubey::math::Vec3 sun_direction = cloud_ref_2_source_sun_direction();
    const cubey::math::Vec3 cloud_top_color{1.12F, 1.04F, 0.82F};
    const cubey::math::Vec3 cloud_bottom_color{0.24F, 0.30F, 0.38F};
    return {
        .camera_right_aspect = {basis.right.x, basis.right.y, basis.right.z, aspect},
        .camera_up_tan_half_fovy = {basis.up.x, basis.up.y, basis.up.z, tan_half_fovy},
        .camera_forward_mode = {basis.forward.x, basis.forward.y, basis.forward.z,
                                cloud_ref_2_camera_shader_mode(config.camera_mode)},
        .camera_position_radius = {basis.position.x, basis.position.y, basis.position.z,
                                   config.planet_radius_m},
        .cloud_shell = {config.bottom_altitude_m,
                        config.top_altitude_m - config.bottom_altitude_m, 0.0F,
                        cloud_ref_2_cloud_style_value(config.cloud_style)},
        .weather = {config.coverage, config.density, config.weather_scale_km,
                    elapsed_seconds * config.wind_speed_mps},
        .sun_direction_intensity = {sun_direction.x, sun_direction.y, sun_direction.z, 1.0F},
        .ref_options = {static_cast<float>(static_cast<std::uint32_t>(config.debug_view)),
                        static_cast<float>(budget.view_steps),
                        static_cast<float>(budget.light_steps),
                        static_cast<float>(target_extent.width)},
        .shape_options = {config.crispiness, config.curliness, config.absorption,
                          config.powder_enabled ? 1.0F : 0.0F},
        .weather_feature_weights = {config.weather_fronts, config.weather_cells,
                                    config.weather_streaks, config.detail_erosion},
        .cloud_color_top_shadow = {cloud_top_color.x, cloud_top_color.y, cloud_top_color.z,
                                   config.shadow_strength},
        .cloud_color_bottom_horizon = {cloud_bottom_color.x, cloud_bottom_color.y,
                                       cloud_bottom_color.z, config.horizon_strength},
        .cache_status = {cache.blend_amount, static_cast<float>(cache.texture_size),
                         static_cast<float>(cache.frames_to_update),
                         static_cast<float>(cache.update_region_size)},
        .cache_region = {static_cast<float>(cache.update_x), static_cast<float>(cache.update_y),
                         static_cast<float>(cache.update_region_size),
                         static_cast<float>(cache.texture_size)},
    };
}

[[nodiscard]] cubey::render::Texture3DConfig cloud_ref_2_volume_texture_config(
    std::uint32_t size) {
    const std::uint32_t mip_levels = cloud_ref_2_mip_count(size);
    return {
        .extent = {size, size, size},
        .mip_levels = mip_levels,
        .format = kCloudRef2NoiseFormat,
        .create_sampler = true,
        .sampler = cloud_ref_2_repeat_sampler_config(mip_levels),
    };
}

[[nodiscard]] cubey::render::Texture2DConfig cloud_ref_2_weather_texture_config() {
    return {
        .extent = {kWeatherTextureSize, kWeatherTextureSize},
        .format = kCloudRef2NoiseFormat,
        .usage = cubey::render::Texture2DUsage::StorageSampled,
        .create_sampler = true,
        .sampler = cloud_ref_2_repeat_sampler_config(),
    };
}

[[nodiscard]] cubey::render::Texture2DConfig cloud_ref_2_cache_texture_config(
    std::uint32_t size) {
    return {
        .extent = {size, size},
        .format = kCloudRef2ColorFormat,
        .usage = cubey::render::Texture2DUsage::StorageSampled,
        .create_sampler = true,
        .sampler =
            {
                .min_filter = VK_FILTER_LINEAR,
                .mag_filter = VK_FILTER_LINEAR,
                .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            },
    };
}

[[nodiscard]] cubey::vulkan::ImageLayoutTransition cloud_ref_2_begin_cache_write_transition(
    VkImage image, VkImageLayout old_layout) {
    return {
        .image = image,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .old_layout = old_layout,
        .new_layout = VK_IMAGE_LAYOUT_GENERAL,
        .src_access_mask =
            old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                ? VK_ACCESS_SHADER_READ_BIT
                : static_cast<VkAccessFlags>(0),
        .dst_access_mask = VK_ACCESS_SHADER_WRITE_BIT,
        .src_stage_mask = old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                              ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                              : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dst_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    };
}

void generate_storage_texture(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                              const char* label, const std::filesystem::path& shader,
                              VkImage image, VkImageView view,
                              cubey::render::ComputeDispatchGroups groups) {
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_image(descriptors.set(), 0, view, VK_IMAGE_LAYOUT_GENERAL);
    writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> layouts{descriptors.layout()};
    const cubey::render::ComputePipelineResource pipeline(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(shader),
                    .descriptor_set_layouts = layouts,
                });

    static_cast<void>(gpu.submit_and_wait(cubey::vulkan::GpuWorkRequest{
        .label = label,
        .work =
            [image, &pipeline, descriptor_set = descriptors.set(),
             groups](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(image));
                cubey::render::record_compute_pipeline_dispatch(
                    recorder,
                    cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set,
                                                                  groups));
                recorder.transition_image_layout(
                    cubey::vulkan::finish_storage_image_write_for_sampling_transition(image));
                commands.submit_and_wait();
            },
    }));
}

void generate_storage_volume_texture(const cubey::vulkan::Device& device,
                                     cubey::vulkan::GpuRuntime& gpu, const char* label,
                                     const std::filesystem::path& shader,
                                     const cubey::render::Texture3D& texture,
                                     cubey::render::ComputeDispatchGroups groups) {
    const cubey::vulkan::ImageView storage_view(
        device, cubey::vulkan::ImageViewConfig{
                    .image = texture.handle(),
                    .format = texture.format(),
                    .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                    .view_type = VK_IMAGE_VIEW_TYPE_3D,
                    .base_mip_level = 0,
                    .level_count = 1,
                });

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_image(descriptors.set(), 0, storage_view.handle(), VK_IMAGE_LAYOUT_GENERAL);
    writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> layouts{descriptors.layout()};
    const cubey::render::ComputePipelineResource pipeline(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(shader),
                    .descriptor_set_layouts = layouts,
                });

    static_cast<void>(gpu.submit_and_wait(cubey::vulkan::GpuWorkRequest{
        .label = label,
        .work =
            [&texture, &pipeline, descriptor_set = descriptors.set(),
             groups](cubey::vulkan::GpuOwnerContext& context) {
                cubey::vulkan::ImmediateCommands commands(context);
                const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(texture.handle()));
                cubey::render::record_compute_pipeline_dispatch(
                    recorder,
                    cubey::render::compute_pipeline_dispatch_info(pipeline, descriptor_set,
                                                                  groups));
                if (texture.mip_levels() > 1U) {
                    cubey::render::record_generate_texture_3d_mips(
                        commands.command_buffer(), texture, VK_IMAGE_LAYOUT_GENERAL);
                } else {
                    recorder.transition_image_layout(
                        cubey::vulkan::finish_storage_image_write_for_sampling_transition(
                            texture.handle()));
                }
                commands.submit_and_wait();
            },
    }));
}

class CloudRef2App {
  public:
    explicit CloudRef2App(RunConfig config)
        : run_config_(std::move(config)), config_(clouds_config_from_run_config(run_config_)) {}

    CloudRef2App(const CloudRef2App&) = delete;
    CloudRef2App& operator=(const CloudRef2App&) = delete;

    ~CloudRef2App() {
        destroy_swapchain_resources();
        destroy_global_resources();
    }

    int run() {
        if (run_config_.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources(context.device(), context.gpu());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) { draw_ui(); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context.device(), frame.command_buffer,
                                  cubey::render::swapchain_color_target_view(context.swapchain(),
                                                                             frame.image_index),
                                  frame.frame_slot);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
            latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;
            latest_frame_ms_ = timing.delta_seconds * 1000.0;
            return cubey::host::FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = context.swapchain().extent().width,
                .height = context.swapchain().extent().height,
                .triangles = 2U,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
            destroy_global_resources();
        };
        return cubey::host::run_windowed_app(
            {
                .run_config = run_config_,
                .app_name = "cloud_ref_2",
                .ready_status = "rendering godot cached-sky reference cloud project",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = run_config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_global_resources(context.device(), context.gpu());
            create_pipeline(context.device(), target.format, target.extent,
                            cubey::host::headless_capture_frame_slot_count(context.config()));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            update_headless_time(frame);
            record_headless_target(context.device(), command_buffer, target, frame.frame_slot);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
            destroy_global_resources();
        };
        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_config();
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            config_.debug_view = next_cloud_ref_2_debug_view(config_.debug_view);
        }
        if (input.key_pressed(cubey::input::Key::Space)) {
            config_.time.playing = !config_.time.playing;
        }
        if (input.mouse_enabled() && input.mouse_button_down(cubey::input::MouseButton::Left)) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Left);
            yaw_ += static_cast<float>(delta.x) * kCameraDragRadiansPerPixel;
            pitch_ = cloud_ref_2_clamp_pitch(
                config_.camera_mode,
                pitch_ - static_cast<float>(delta.y) * kCameraDragRadiansPerPixel);
        }
        advance_clouds_time(config_, timing.delta_seconds);
        elapsed_seconds_ += static_cast<float>(timing.delta_seconds);
        validate_clouds_config(config_);
    }

    void update_headless_time(const cubey::host::HeadlessCaptureFrame& frame) {
        CloudsConfig frame_config = config_;
        frame_config.time.time_hours =
            std::fmod(config_.time.time_hours +
                          static_cast<float>(frame.index) *
                              static_cast<float>(frame.timing.delta_seconds) *
                              config_.time.speed_hours_per_second,
                      24.0F);
        config_ = frame_config;
        elapsed_seconds_ = static_cast<float>(frame.timing.elapsed_seconds);
    }

    [[nodiscard]] CloudsDebugView next_cloud_ref_2_debug_view(CloudsDebugView view) const {
        const auto found = std::find(kCloudRef2DebugViews.begin(), kCloudRef2DebugViews.end(), view);
        if (found == kCloudRef2DebugViews.end() || std::next(found) == kCloudRef2DebugViews.end()) {
            return kCloudRef2DebugViews.front();
        }
        return *std::next(found);
    }

    void draw_ui() {
        ImGui::SetNextWindowPos(ImVec2(12.0F, 12.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420.0F, 650.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Cloud Ref 2")) {
            ImGui::End();
            return;
        }
        if (ImGui::Button("Reset")) {
            reset_config();
        }
        ImGui::SameLine();
        if (ImGui::Button(config_.time.playing ? "Pause" : "Play")) {
            config_.time.playing = !config_.time.playing;
        }
        draw_enum_combo("Camera", config_.camera_mode, kCloudRef2CameraModes,
                        clouds_camera_mode_name);
        draw_enum_combo("Weather", config_.weather_preset, kCloudRef2WeatherPresets,
                        clouds_weather_preset_name);
        draw_enum_combo("Quality", config_.quality, kCloudRef2QualityModes, clouds_quality_name);
        draw_enum_combo("Debug", config_.debug_view, kCloudRef2DebugViews, clouds_debug_view_name);
        draw_enum_combo("Cache frames", config_.cache_frames, kCloudRef2CacheFrameModes,
                        clouds_cache_frames_name);
        if (ImGui::BeginCombo("Cache size", std::to_string(config_.cache_texture_size).c_str())) {
            for (std::uint32_t size : kCloudRef2CacheTextureSizes) {
                const bool selected = size == config_.cache_texture_size;
                if (ImGui::Selectable(std::to_string(size).c_str(), selected)) {
                    config_.cache_texture_size = size;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();
        ImGui::SliderFloat("Time", &config_.time.time_hours, 0.0F, 24.0F, "%.2f h");
        ImGui::SliderFloat("Coverage", &config_.coverage, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Density", &config_.density, 0.0F, 0.08F, "%.3f");
        ImGui::SliderFloat("Wind", &config_.wind_speed_mps, 0.0F, 900.0F, "%.0f m/s");
        ImGui::SliderFloat("Crispiness", &config_.crispiness, 1.0F, 120.0F, "%.1f");
        ImGui::SliderFloat("Curliness", &config_.curliness, 0.01F, 3.0F, "%.2f");
        ImGui::SliderFloat("Absorption", &config_.absorption, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Shadow strength", &config_.shadow_strength, 0.0F, 2.0F, "%.2f");
        ImGui::SliderFloat("Horizon fill", &config_.horizon_strength, 0.0F, 2.0F, "%.2f");
        ImGui::Checkbox("Powder", &config_.powder_enabled);
        ImGui::SliderFloat("Weather scale", &config_.weather_scale_km, 40.0F, 500.0F, "%.0f km");
        ImGui::Separator();
        ImGui::Text("FPS: %.1f / %.2f ms", latest_fps_, latest_frame_ms_);
        ImGui::Text("Base noise: %u^3", kBaseNoiseSize);
        ImGui::Text("Detail noise: %u^3", kDetailNoiseSize);
        ImGui::Text("Weather texture: %u x %u", kWeatherTextureSize, kWeatherTextureSize);
        const CloudRef2CacheUniformState cache_state = cache_uniform_state();
        ImGui::Text("Cache: %u x %u / %u frames", cache_state.texture_size,
                    cache_state.texture_size, cache_state.frames_to_update);
        ImGui::Text("Update: %u,%u %u px / blend %.2f", cache_state.update_x,
                    cache_state.update_y, cache_state.update_region_size,
                    cache_state.blend_amount);
        ImGui::End();
    }

    template <typename T, std::size_t N, typename NameFn>
    void draw_enum_combo(const char* label, T& value, const std::array<T, N>& values,
                         NameFn name_fn) {
        if (ImGui::BeginCombo(label, name_fn(value))) {
            for (T candidate : values) {
                const bool selected = candidate == value;
                if (ImGui::Selectable(name_fn(candidate), selected)) {
                    value = candidate;
                    if constexpr (std::is_same_v<T, CloudsCameraMode>) {
                        config_.camera_altitude_m = clouds_default_camera_altitude_m(value);
                        pitch_ = cloud_ref_2_clamp_pitch(value, 0.0F);
                    } else if constexpr (std::is_same_v<T, CloudsWeatherPreset>) {
                        apply_clouds_weather_preset(config_, value);
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    void reset_config() {
        config_ = clouds_config_from_run_config(run_config_);
        if (std::find(kCloudRef2CameraModes.begin(), kCloudRef2CameraModes.end(),
                      config_.camera_mode) == kCloudRef2CameraModes.end()) {
            config_.camera_mode = CloudsCameraMode::Surface;
        }
        config_.camera_altitude_m = clouds_default_camera_altitude_m(config_.camera_mode);
        yaw_ = 0.0F;
        pitch_ = cloud_ref_2_clamp_pitch(config_.camera_mode, 0.0F);
        elapsed_seconds_ = 0.0F;
        reset_cache_state();
    }

    void create_global_resources(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu) {
        if (base_noise_.has_value()) {
            return;
        }

        base_noise_.emplace(device, cloud_ref_2_volume_texture_config(kBaseNoiseSize));
        detail_noise_.emplace(device, cloud_ref_2_volume_texture_config(kDetailNoiseSize));
        weather_texture_.emplace(device, cloud_ref_2_weather_texture_config());

        generate_storage_volume_texture(
            device, gpu, "cloud_ref_2 generate base noise",
            shader_path("cloud_ref_2_perlin_worley.comp.spv"), base_noise_.value(),
            cubey::render::ceil_dispatch_groups(kBaseNoiseSize, kBaseNoiseSize, kBaseNoiseSize,
                                                kCloudRef2VolumeGroupSize));
        generate_storage_volume_texture(
            device, gpu, "cloud_ref_2 generate detail noise", shader_path("cloud_ref_2_worley.comp.spv"),
            detail_noise_.value(),
            cubey::render::ceil_dispatch_groups(kDetailNoiseSize, kDetailNoiseSize,
                                                kDetailNoiseSize, kCloudRef2VolumeGroupSize));
        generate_storage_texture(
            device, gpu, "cloud_ref_2 generate weather", shader_path("cloud_ref_2_weather.comp.spv"),
            weather_texture_->handle(), weather_texture_->view(),
            cubey::render::ceil_dispatch_groups(kWeatherTextureSize, kWeatherTextureSize,
                                                kCloudRef2ComputeGroupSize));
    }

    void destroy_global_resources() {
        weather_texture_.reset();
        detail_noise_.reset();
        base_noise_.reset();
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        cache_material_.reset();
        composite_material_.reset();
        cache_pipeline_.reset();
        composite_pipeline_.reset();
        frame_uniforms_.reset();
        cache_textures_ = {};
        cache_texture_size_ = 0U;
        cache_layouts_.fill(VK_IMAGE_LAYOUT_UNDEFINED);
        reset_cache_state();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count) {
        graph_executor_.resize(frame_slot_count);
        frame_uniforms_.emplace(device, frame_slot_count);

        const cubey::render::MaterialPassInfo cache_pass = cloud_ref_2_cache_pass_info();
        cache_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                            .material_pass = cache_pass,
                                            .descriptor_set = 0,
                                            .set_count =
                                                frame_slot_count * kCloudRef2CacheTextureCount,
                                        });
        const std::array<VkDescriptorSetLayout, 1> cache_layouts{cache_material_->layout()};
        const std::array<VkPushConstantRange, 1> cache_push_constants{{
            VkPushConstantRange{
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .offset = 0,
                .size = sizeof(CloudRef2CachePushConstants),
            },
        }};
        cache_pipeline_.emplace(device, cubey::render::ComputePipelineResourceConfig{
                                            .shader_stage = cubey::render::compute_shader_file(
                                                shader_path("cloud_ref_2_cache.comp.spv")),
                                            .descriptor_set_layouts = cache_layouts,
                                            .push_constants = cache_push_constants,
                                        });

        const cubey::render::MaterialPassInfo composite_pass = cloud_ref_2_composite_pass_info();
        composite_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                .material_pass = composite_pass,
                                                .descriptor_set = 0,
                                                .set_count = frame_slot_count,
                                            });
        const std::array<VkDescriptorSetLayout, 1> composite_layouts{
            composite_material_->layout()};
        const std::array<cubey::render::ShaderStageFile, 2> composite_shaders{
            cubey::render::vertex_shader_file(shader_path("cloud_ref_2.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("cloud_ref_2_composite.frag.spv")),
        };
        composite_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                                .extent = extent,
                                                .color_format = color_format,
                                                .shader_stage_files = composite_shaders,
                                                .descriptor_set_layouts = composite_layouts,
                                                .material_pass = composite_pass,
                                            });
        ensure_cache_textures(device);
    }

    [[nodiscard]] const cubey::render::Texture3D& base_noise() const {
        if (!base_noise_.has_value()) {
            throw std::runtime_error("cloud_ref_2 base noise texture is not initialized");
        }
        return base_noise_.value();
    }

    [[nodiscard]] const cubey::render::Texture3D& detail_noise() const {
        if (!detail_noise_.has_value()) {
            throw std::runtime_error("cloud_ref_2 detail noise texture is not initialized");
        }
        return detail_noise_.value();
    }

    [[nodiscard]] const cubey::render::Texture2D& weather_texture() const {
        if (!weather_texture_.has_value()) {
            throw std::runtime_error("cloud_ref_2 weather texture is not initialized");
        }
        return weather_texture_.value();
    }

    [[nodiscard]] const cubey::render::FrameUniformBuffer<CloudRef2FrameUniforms>&
    frame_uniforms() const {
        if (!frame_uniforms_.has_value()) {
            throw std::runtime_error("cloud_ref_2 uniforms are not initialized");
        }
        return frame_uniforms_.value();
    }

    [[nodiscard]] const cubey::render::ComputePipelineResource& cache_pipeline() const {
        if (!cache_pipeline_.has_value()) {
            throw std::runtime_error("cloud_ref_2 cache pipeline is not initialized");
        }
        return cache_pipeline_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& composite_pipeline() const {
        if (!composite_pipeline_.has_value()) {
            throw std::runtime_error("cloud_ref_2 composite pipeline is not initialized");
        }
        return composite_pipeline_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& cache_material() const {
        if (!cache_material_.has_value()) {
            throw std::runtime_error("cloud_ref_2 cache material is not initialized");
        }
        return cache_material_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& composite_material() const {
        if (!composite_material_.has_value()) {
            throw std::runtime_error("cloud_ref_2 composite material is not initialized");
        }
        return composite_material_.value();
    }

    [[nodiscard]] const cubey::render::Texture2D& cache_texture(std::uint32_t index) const {
        if (index >= kCloudRef2CacheTextureCount || !cache_textures_[index].has_value()) {
            throw std::runtime_error("cloud_ref_2 cache texture is not initialized");
        }
        return cache_textures_[index].value();
    }

    [[nodiscard]] cubey::render::FrameSlot cache_descriptor_slot(
        cubey::render::FrameSlot frame_slot, std::uint32_t texture_index) const {
        if (texture_index >= kCloudRef2CacheTextureCount) {
            throw std::runtime_error("cloud_ref_2 cache descriptor texture index is invalid");
        }
        return {
            .index = frame_slot.index * kCloudRef2CacheTextureCount + texture_index,
            .count = frame_slot.count * kCloudRef2CacheTextureCount,
        };
    }

    void reset_cache_state() {
        cache_initialized_ = false;
        cache_update_tile_ = 0U;
        cache_update_texture_ = 0U;
        cache_blend_from_texture_ = 1U;
        cache_blend_to_texture_ = 2U;
        cache_completed_cycles_ = 0U;
        cache_frames_ = config_.cache_frames;
    }

    [[nodiscard]] CloudRef2CacheUniformState cache_uniform_state() const {
        const std::uint32_t texture_size =
            cache_texture_size_ == 0U ? config_.cache_texture_size : cache_texture_size_;
        const std::uint32_t frames_to_update = clouds_cache_frames_value(config_.cache_frames);
        const std::uint32_t grid = clouds_cache_frame_grid_size(config_.cache_frames);
        const std::uint32_t region =
            clouds_cache_update_region_size(texture_size, config_.cache_frames);
        const std::uint32_t tile = std::min(cache_update_tile_, frames_to_update - 1U);
        return {
            .blend_amount = cache_initialized_ && cache_completed_cycles_ > 0U
                                ? std::clamp(static_cast<float>(cache_update_tile_) /
                                                 static_cast<float>(frames_to_update),
                                             0.0F, 1.0F)
                                : 1.0F,
            .texture_size = texture_size,
            .update_region_size = region,
            .update_x = (tile % grid) * region,
            .update_y = (tile / grid) * region,
            .frames_to_update = frames_to_update,
        };
    }

    void upload_uniforms(cubey::render::FrameSlot frame_slot, VkExtent2D target_extent,
                         CloudRef2CacheUniformState cache_state) const {
        frame_uniforms().upload(
            frame_slot,
            cloud_ref_2_frame_uniforms(config_, target_extent, yaw_, pitch_, elapsed_seconds_,
                                       cache_state));
    }

    void update_cache_descriptors(const cubey::vulkan::Device& device) const {
        const std::uint32_t frame_slot_count = frame_uniforms().slot_count();
        for (std::uint32_t slot_index = 0; slot_index < frame_slot_count; ++slot_index) {
            const cubey::render::FrameSlot frame_slot{
                .index = slot_index,
                .count = frame_slot_count,
            };
            for (std::uint32_t texture_index = 0; texture_index < kCloudRef2CacheTextureCount;
                 ++texture_index) {
                cubey::render::MaterialDescriptorWriter(
                    cache_material().set(cache_descriptor_slot(frame_slot, texture_index)))
                    .uniform_buffer(kCloudRef2UniformBinding,
                                    frame_uniforms().buffer(frame_slot).handle(),
                                    frame_uniforms().range())
                    .storage_image(kCloudRef2OutputBinding, cache_texture(texture_index).view(),
                                   VK_IMAGE_LAYOUT_GENERAL)
                    .combined_image_sampler(kCloudRef2BaseNoiseBinding,
                                            base_noise().sampler().handle(), base_noise().view())
                    .combined_image_sampler(kCloudRef2DetailNoiseBinding,
                                            detail_noise().sampler().handle(),
                                            detail_noise().view())
                    .combined_image_sampler(kCloudRef2WeatherBinding,
                                            weather_texture().sampler().handle(),
                                            weather_texture().view())
                    .update(device);
            }
        }
    }

    void update_composite_descriptor(const cubey::vulkan::Device& device,
                                     cubey::render::FrameSlot frame_slot) const {
        const cubey::render::Texture2D& blend_from = cache_texture(cache_blend_from_texture_);
        const cubey::render::Texture2D& blend_to = cache_texture(cache_blend_to_texture_);
        cubey::render::MaterialDescriptorWriter(composite_material().set(frame_slot))
            .uniform_buffer(kCloudRef2UniformBinding, frame_uniforms().buffer(frame_slot).handle(),
                            frame_uniforms().range())
            .combined_image_sampler(kCloudRef2CompositeBlendFromBinding,
                                    blend_from.sampler().handle(), blend_from.view(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .combined_image_sampler(kCloudRef2CompositeBlendToBinding,
                                    blend_to.sampler().handle(), blend_to.view(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .update(device);
    }

    void ensure_cache_textures(const cubey::vulkan::Device& device) {
        if (cache_frames_ != config_.cache_frames) {
            reset_cache_state();
        }
        if (cache_texture_size_ == config_.cache_texture_size && cache_textures_[0].has_value()) {
            return;
        }

        validate_clouds_config(config_);
        for (std::uint32_t index = 0; index < kCloudRef2CacheTextureCount; ++index) {
            cache_textures_[index].emplace(device,
                                           cloud_ref_2_cache_texture_config(
                                               config_.cache_texture_size));
        }
        cache_texture_size_ = config_.cache_texture_size;
        cache_layouts_.fill(VK_IMAGE_LAYOUT_UNDEFINED);
        reset_cache_state();
        update_cache_descriptors(device);
    }

    void record_cache_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                               cubey::render::FrameSlot frame_slot, std::uint32_t texture_index,
                               CloudRef2CachePushConstants push) {
        cubey::render::ComputeDispatchGroups groups =
            cubey::render::ceil_dispatch_groups(
                static_cast<std::uint32_t>(std::round(push.update_region.z)),
                static_cast<std::uint32_t>(std::round(push.update_region.z)),
                kCloudRef2CacheGroupSize);
        recorder.transition_image_layout(cloud_ref_2_begin_cache_write_transition(
            cache_texture(texture_index).handle(), cache_layouts_[texture_index]));
        cache_layouts_[texture_index] = VK_IMAGE_LAYOUT_GENERAL;
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                cache_pipeline(),
                cache_material().set(cache_descriptor_slot(frame_slot, texture_index)), groups),
            VK_SHADER_STAGE_COMPUTE_BIT, push);
        recorder.transition_image_layout(
            cubey::vulkan::finish_storage_image_write_for_sampling_transition(
                cache_texture(texture_index).handle()));
        cache_layouts_[texture_index] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void record_cache_clear(const cubey::vulkan::CommandRecorder& recorder,
                            cubey::render::FrameSlot frame_slot) {
        const float texture_size = static_cast<float>(cache_texture_size_);
        for (std::uint32_t index = 0; index < kCloudRef2CacheTextureCount; ++index) {
            record_cache_dispatch(
                recorder, frame_slot, index,
                CloudRef2CachePushConstants{
                    .update_region = {0.0F, 0.0F, texture_size, texture_size},
                    .cache_options = {1.0F, 0.0F, 0.0F, 0.0F},
                });
        }
        cache_initialized_ = true;
    }

    void advance_cache_update() {
        const std::uint32_t frames_to_update = clouds_cache_frames_value(config_.cache_frames);
        cache_update_tile_ = std::min(cache_update_tile_ + 1U, frames_to_update);
        if (cache_update_tile_ < frames_to_update) {
            return;
        }

        const std::uint32_t completed_texture = cache_update_texture_;
        const std::uint32_t reusable_texture = cache_blend_from_texture_;
        if (cache_completed_cycles_ == 0U) {
            cache_blend_from_texture_ = completed_texture;
            cache_blend_to_texture_ = completed_texture;
        } else {
            cache_blend_from_texture_ = cache_blend_to_texture_;
            cache_blend_to_texture_ = completed_texture;
        }
        cache_update_texture_ = reusable_texture;
        cache_update_tile_ = 0U;
        ++cache_completed_cycles_;
    }

    void record_cache_update(const cubey::vulkan::CommandRecorder& recorder,
                             cubey::render::FrameSlot frame_slot,
                             CloudRef2CacheUniformState cache_state) {
        if (!cache_initialized_) {
            record_cache_clear(recorder, frame_slot);
        }

        record_cache_dispatch(
            recorder, frame_slot, cache_update_texture_,
            CloudRef2CachePushConstants{
                .update_region = {static_cast<float>(cache_state.update_x),
                                  static_cast<float>(cache_state.update_y),
                                  static_cast<float>(cache_state.update_region_size),
                                  static_cast<float>(cache_state.texture_size)},
                .cache_options = {0.0F, 0.0F, 0.0F, 0.0F},
            });
        advance_cache_update();
    }

    void record_composite_draw(const cubey::vulkan::CommandRecorder& recorder,
                               const cubey::render::ColorTargetView& target,
                               cubey::render::FrameSlot frame_slot) const {
        cubey::render::record_render_target_pass(
            recorder, cubey::render::render_target_view(target),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            },
            [this, frame_slot](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder,
                    {.pipeline = &composite_pipeline(),
                     .descriptor_set = composite_material().set(frame_slot)});
            });
    }

    [[nodiscard]] CloudRef2FrameGraph build_frame_graph(cubey::render::ColorTargetView target,
                                                       cubey::render::FrameSlot frame_slot,
                                                       CloudRef2TargetMode mode) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer =
            graph.import_color_target("backbuffer", target, cloud_ref_2_target_initial_state(mode),
                                      cloud_ref_2_target_final_state(mode));
        graph.add_pass("cloud_ref_2 composite", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(backbuffer)
            .execute([this, backbuffer, frame_slot](
                         const cubey::render::RenderGraphExecutionContext& context) {
                record_composite_draw(context.recorder(),
                                      cubey::render::resolved_color_target_view(context, backbuffer),
                                      frame_slot);
            });
        return {
            .graph = graph.compile(),
        };
    }

    void record_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                       const cubey::render::ColorTargetView& target,
                       cubey::render::FrameSlot frame_slot, CloudRef2TargetMode mode,
                       cubey::render::RenderGraphCommandBufferMode command_buffer_mode,
                       const char* label) {
        ensure_cache_textures(device);
        upload_uniforms(frame_slot, target.extent, cache_uniform_state());
        const CloudRef2FrameGraph frame_graph = build_frame_graph(target, frame_slot, mode);
        const auto record_graph = [this, &device, command_buffer, frame_slot,
                                   &frame_graph]() {
            graph_executor_.record(
                cubey::render::RenderGraphFrameRecordInfo{
                    .device = &device,
                    .command_buffer = command_buffer,
                    .frame_slot = frame_slot,
                    .command_buffer_mode =
                        cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                },
                frame_graph.graph);
        };
        const auto record_cache_updates = [this, &device, &target,
                                           frame_slot](const cubey::vulkan::CommandRecorder&
                                                           recorder) {
            const std::uint32_t update_count =
                run_config_.headless ? std::max(1U, run_config_.frames) : 1U;
            for (std::uint32_t update = 0; update < update_count; ++update) {
                record_cache_update(recorder, frame_slot, cache_uniform_state());
            }
            upload_uniforms(frame_slot, target.extent, cache_uniform_state());
            update_composite_descriptor(device, frame_slot);
        };

        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        if (command_buffer_mode == cubey::render::RenderGraphCommandBufferMode::BeginAndEnd) {
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            record_cache_updates(recorder);
            record_graph();
            recorder.end(label != nullptr ? label : "vkEndCommandBuffer cloud_ref_2");
            return;
        }

        record_cache_updates(recorder);
        record_graph();
    }

    void record_windowed_frame(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                               const cubey::render::ColorTargetView& target,
                               cubey::render::FrameSlot frame_slot) {
        record_target(device, command_buffer, target, frame_slot, CloudRef2TargetMode::Present,
                      cubey::render::RenderGraphCommandBufferMode::BeginAndEnd,
                      "vkEndCommandBuffer cloud_ref_2");
    }

    void record_headless_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                                const cubey::render::ColorTargetView& target,
                                cubey::render::FrameSlot frame_slot) {
        record_target(device, command_buffer, target, frame_slot,
                      CloudRef2TargetMode::ColorAttachment,
                      cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                      "vkEndCommandBuffer cloud_ref_2 headless");
    }

    RunConfig run_config_;
    CloudsConfig config_{};
    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    float elapsed_seconds_ = 0.0F;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    std::optional<cubey::render::Texture3D> base_noise_{};
    std::optional<cubey::render::Texture3D> detail_noise_{};
    std::optional<cubey::render::Texture2D> weather_texture_{};
    std::array<std::optional<cubey::render::Texture2D>, kCloudRef2CacheTextureCount>
        cache_textures_{};
    std::array<VkImageLayout, kCloudRef2CacheTextureCount> cache_layouts_{
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_UNDEFINED,
    };
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
    std::optional<cubey::render::FrameUniformBuffer<CloudRef2FrameUniforms>> frame_uniforms_{};
    std::optional<cubey::render::MaterialInstance> cache_material_{};
    std::optional<cubey::render::MaterialInstance> composite_material_{};
    std::optional<cubey::render::ComputePipelineResource> cache_pipeline_{};
    std::optional<cubey::render::GraphicsPipelineResource> composite_pipeline_{};
    CloudsCacheFrames cache_frames_ = CloudsCacheFrames::Frames64;
    std::uint32_t cache_texture_size_ = 0U;
    std::uint32_t cache_update_tile_ = 0U;
    std::uint32_t cache_update_texture_ = 0U;
    std::uint32_t cache_blend_from_texture_ = 1U;
    std::uint32_t cache_blend_to_texture_ = 2U;
    std::uint32_t cache_completed_cycles_ = 0U;
    bool cache_initialized_ = false;
};

} // namespace

int run_cloud_ref_2(const RunConfig& config) {
    CloudRef2App app(config);
    return app.run();
}

} // namespace cubey::projects::cloud_ref_2
