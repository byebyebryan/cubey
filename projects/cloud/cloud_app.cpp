#include "cloud_app.h"

#include "cloud_config.h"

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

#ifndef CUBEY_CLOUD_SHADER_DIR
#error "CUBEY_CLOUD_SHADER_DIR must be defined by the cloud CMake target"
#endif

namespace cubey::projects::cloud {
namespace {

using cubey::FrameTiming;

constexpr float kDefaultFovyRadians = 62.0F * (glm::pi<float>() / 180.0F);
constexpr float kCameraDragRadiansPerPixel = 0.006F;
constexpr float kSurfaceMinPitchRadians = -1.50F;
constexpr float kSurfaceMaxPitchRadians = 1.35F;
constexpr std::uint32_t kBaseNoiseSize = 128U;
constexpr std::uint32_t kDetailNoiseSize = 32U;
constexpr std::uint32_t kWeatherTextureSize = 1024U;
constexpr std::uint32_t kCloudComputeGroupSize = 16U;
constexpr std::uint32_t kCloudVolumeGroupSize = 4U;
constexpr VkFormat kCloudColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kCloudNoiseFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kCloudUniformBinding = 0;
constexpr std::uint32_t kCloudOutputBinding = 1;
constexpr std::uint32_t kCloudBaseNoiseBinding = 2;
constexpr std::uint32_t kCloudDetailNoiseBinding = 3;
constexpr std::uint32_t kCloudWeatherBinding = 4;
constexpr std::uint32_t kCloudMetadataBinding = 5;
constexpr std::uint32_t kCloudCompositeCloudBinding = 1;
constexpr std::uint32_t kCloudCompositeMetadataBinding = 2;

constexpr std::array<CloudsCameraMode, 6> kCloudCameraModes{
    CloudsCameraMode::Surface, CloudsCameraMode::SurfaceUp, CloudsCameraMode::High,
    CloudsCameraMode::HighOblique, CloudsCameraMode::Orbit, CloudsCameraMode::OrbitTerminator,
};
constexpr std::array<CloudsQuality, 3> kCloudQualityModes{
    CloudsQuality::Quarter,
    CloudsQuality::Half,
    CloudsQuality::Full,
};
constexpr std::array<CloudsWeatherPreset, 5> kCloudWeatherPresets{
    CloudsWeatherPreset::FairWeather,
    CloudsWeatherPreset::BrokenCumulus,
    CloudsWeatherPreset::OvercastStratus,
    CloudsWeatherPreset::StormCells,
    CloudsWeatherPreset::HighCirrus,
};
constexpr std::array<CloudsSamplingMode, 3> kCloudSamplingModes{
    CloudsSamplingMode::Interleaved,
    CloudsSamplingMode::Bayer,
    CloudsSamplingMode::Off,
};
constexpr std::array<CloudsDebugView, 21> kCloudDebugViews{
    CloudsDebugView::Final,        CloudsDebugView::RawFinal, CloudsDebugView::Weather,
    CloudsDebugView::Density,      CloudsDebugView::Transmittance,
    CloudsDebugView::Lighting,     CloudsDebugView::AmbientLight,
    CloudsDebugView::DirectLight,  CloudsDebugView::PhaseLight,
    CloudsDebugView::Shadow,       CloudsDebugView::Steps,    CloudsDebugView::Background,
    CloudsDebugView::CloudAlpha,   CloudsDebugView::Distance,
    CloudsDebugView::MetadataDistance,
    CloudsDebugView::MetadataAlpha,
    CloudsDebugView::MetadataConfidence,
    CloudsDebugView::MetadataDensity,
    CloudsDebugView::BaseDensity,  CloudsDebugView::DetailDensity,
    CloudsDebugView::CloudType,
};

struct CloudFrameUniforms {
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
    cubey::math::Vec4 lighting_strengths;
    cubey::math::Vec4 composite_options;
    cubey::math::Vec4 sampling_options;
};

static_assert(sizeof(CloudFrameUniforms) == sizeof(float) * 60U);

struct CloudViewBasis {
    cubey::math::Vec3 position{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
};

enum class CloudTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

struct CloudFrameGraph {
    cubey::render::CompiledRenderGraph graph{};
    cubey::render::RenderGraphTextureHandle cloud_product{};
    cubey::render::RenderGraphTextureHandle cloud_metadata{};
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_CLOUD_SHADER_DIR) / filename;
}

[[nodiscard]] std::uint32_t cloud_mip_count(std::uint32_t size) {
    std::uint32_t levels = 1;
    while (size > 1U) {
        size = std::max(1U, size / 2U);
        ++levels;
    }
    return levels;
}

[[nodiscard]] cubey::vulkan::SamplerConfig cloud_repeat_sampler_config(
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

[[nodiscard]] cubey::render::MaterialDescriptorSetLayout cloud_march_set_layout() {
    return {
        .set = 0,
        .bindings =
            {
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudUniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudOutputBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudBaseNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudDetailNoiseBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudWeatherBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudMetadataBinding,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
            },
    };
}

[[nodiscard]] cubey::render::MaterialDescriptorSetLayout cloud_composite_set_layout() {
    return {
        .set = 0,
        .bindings =
            {
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudUniformBinding,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudCompositeCloudBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                cubey::vulkan::DescriptorSetBindingConfig{
                    .binding = kCloudCompositeMetadataBinding,
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            },
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo cloud_march_pass_info() {
    return {
        .label = "cloud_march",
        .descriptor_sets = {cloud_march_set_layout()},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo cloud_composite_pass_info() {
    return {
        .label = "cloud_composite",
        .descriptor_sets = {cloud_composite_set_layout()},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = false,
        .depth_write = false,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState
cloud_target_initial_state(CloudTargetMode mode) {
    return mode == CloudTargetMode::Present
               ? cubey::render::render_graph_undefined_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] cubey::render::RenderGraphTextureState
cloud_target_final_state(CloudTargetMode mode) {
    return mode == CloudTargetMode::Present
               ? cubey::render::render_graph_present_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
cloud_color_texture_desc(std::string label, VkExtent2D extent) {
    return {
        .label = std::move(label),
        .extent = {extent.width, extent.height, 1U},
        .format = kCloudColorFormat,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

[[nodiscard]] VkExtent2D cloud_product_extent(VkExtent2D target_extent,
                                                  CloudsQuality quality) {
    const CloudsQualityBudget budget = clouds_quality_budget(quality);
    return {
        .width = std::max(1U, static_cast<std::uint32_t>(
                                  std::round(static_cast<float>(target_extent.width) *
                                             budget.resolution_scale))),
        .height = std::max(1U, static_cast<std::uint32_t>(
                                   std::round(static_cast<float>(target_extent.height) *
                                              budget.resolution_scale))),
    };
}

[[nodiscard]] float cloud_camera_base_pitch(CloudsCameraMode mode) {
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

[[nodiscard]] float cloud_clamp_pitch(CloudsCameraMode mode, float pitch_offset) {
    const float base_pitch = cloud_camera_base_pitch(mode);
    return std::clamp(base_pitch + pitch_offset, kSurfaceMinPitchRadians, kSurfaceMaxPitchRadians) -
           base_pitch;
}

[[nodiscard]] float cloud_camera_shader_mode(CloudsCameraMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float cloud_cloud_style_value(CloudsCloudStyle style) {
    return static_cast<float>(static_cast<std::uint32_t>(style));
}

[[nodiscard]] float cloud_sampling_mode_value(CloudsSamplingMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] CloudViewBasis cloud_view_basis(const CloudsConfig& config, float yaw,
                                                     float pitch_offset) {
    const cubey::math::Vec3 surface_up{0.0F, 1.0F, 0.0F};
    const float yaw_sin = std::sin(yaw);
    const float yaw_cos = std::cos(yaw);
    const cubey::math::Vec3 flat_forward{yaw_sin, 0.0F, -yaw_cos};
    const cubey::math::Vec3 right{yaw_cos, 0.0F, yaw_sin};
    const float pitch = cloud_camera_base_pitch(config.camera_mode) + pitch_offset;
    const cubey::math::Vec3 forward =
        glm::normalize(flat_forward * std::cos(pitch) + surface_up * std::sin(pitch));
    return {
        .position = {0.0F, config.camera_altitude_m, 0.0F},
        .right = right,
        .up = glm::normalize(glm::cross(right, forward)),
        .forward = forward,
    };
}

[[nodiscard]] cubey::math::Vec3 cloud_source_sun_direction() {
    return glm::normalize(cubey::math::Vec3{-0.5F, 0.5F, 1.0F});
}

[[nodiscard]] CloudFrameUniforms cloud_frame_uniforms(const CloudsConfig& config,
                                                            VkExtent2D target_extent,
                                                            float yaw, float pitch,
                                                            float elapsed_seconds) {
    const CloudViewBasis basis = cloud_view_basis(config, yaw, pitch);
    const float aspect =
        static_cast<float>(target_extent.width) / static_cast<float>(target_extent.height);
    const float tan_half_fovy = std::tan(kDefaultFovyRadians * 0.5F);
    const CloudsQualityBudget budget = clouds_quality_budget(config.quality);
    const cubey::math::Vec3 sun_direction = cloud_source_sun_direction();
    const cubey::math::Vec3 cloud_top_color{1.12F, 1.04F, 0.82F};
    const cubey::math::Vec3 cloud_bottom_color{0.24F, 0.30F, 0.38F};
    return {
        .camera_right_aspect = {basis.right.x, basis.right.y, basis.right.z, aspect},
        .camera_up_tan_half_fovy = {basis.up.x, basis.up.y, basis.up.z, tan_half_fovy},
        .camera_forward_mode = {basis.forward.x, basis.forward.y, basis.forward.z,
                                cloud_camera_shader_mode(config.camera_mode)},
        .camera_position_radius = {basis.position.x, basis.position.y, basis.position.z,
                                   config.planet_radius_m},
        .cloud_shell = {config.bottom_altitude_m,
                        config.top_altitude_m - config.bottom_altitude_m,
                        config.vertical_shear_fraction,
                        cloud_cloud_style_value(config.cloud_style)},
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
        .lighting_strengths = {config.ambient_strength, config.direct_strength,
                               config.phase_strength, config.sun_glare_strength},
        .composite_options = {config.resolve_strength, config.final_contrast,
                              config.final_saturation, config.horizon_glow_strength},
        .sampling_options = {cloud_sampling_mode_value(config.sampling_mode),
                             config.jitter_strength, 0.0F, 0.0F},
    };
}

[[nodiscard]] cubey::render::Texture3DConfig cloud_volume_texture_config(
    std::uint32_t size) {
    const std::uint32_t mip_levels = cloud_mip_count(size);
    return {
        .extent = {size, size, size},
        .mip_levels = mip_levels,
        .format = kCloudNoiseFormat,
        .create_sampler = true,
        .sampler = cloud_repeat_sampler_config(mip_levels),
    };
}

[[nodiscard]] cubey::render::Texture2DConfig cloud_weather_texture_config() {
    return {
        .extent = {kWeatherTextureSize, kWeatherTextureSize},
        .format = kCloudNoiseFormat,
        .usage = cubey::render::Texture2DUsage::StorageSampled,
        .create_sampler = true,
        .sampler = cloud_repeat_sampler_config(),
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

class CloudApp {
  public:
    explicit CloudApp(RunConfig config)
        : run_config_(std::move(config)), config_(clouds_config_from_run_config(run_config_)) {}

    CloudApp(const CloudApp&) = delete;
    CloudApp& operator=(const CloudApp&) = delete;

    ~CloudApp() {
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
                .app_name = "cloud",
                .ready_status = "rendering production cloud project",
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
            config_.debug_view = next_cloud_debug_view(config_.debug_view);
        }
        if (input.key_pressed(cubey::input::Key::Space)) {
            config_.time.playing = !config_.time.playing;
        }
        if (input.mouse_enabled() && input.mouse_button_down(cubey::input::MouseButton::Left)) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Left);
            yaw_ += static_cast<float>(delta.x) * kCameraDragRadiansPerPixel;
            pitch_ = cloud_clamp_pitch(
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

    [[nodiscard]] CloudsDebugView next_cloud_debug_view(CloudsDebugView view) const {
        const auto found = std::find(kCloudDebugViews.begin(), kCloudDebugViews.end(), view);
        if (found == kCloudDebugViews.end() || std::next(found) == kCloudDebugViews.end()) {
            return kCloudDebugViews.front();
        }
        return *std::next(found);
    }

    void draw_ui() {
        ImGui::SetNextWindowPos(ImVec2(12.0F, 12.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420.0F, 650.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Cloud")) {
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
        draw_enum_combo("Camera", config_.camera_mode, kCloudCameraModes,
                        clouds_camera_mode_name);
        draw_enum_combo("Weather", config_.weather_preset, kCloudWeatherPresets,
                        clouds_weather_preset_name);
        draw_enum_combo("Quality", config_.quality, kCloudQualityModes, clouds_quality_name);
        draw_enum_combo("Debug", config_.debug_view, kCloudDebugViews, clouds_debug_view_name);
        ImGui::Separator();
        ImGui::SliderFloat("Time", &config_.time.time_hours, 0.0F, 24.0F, "%.2f h");
        ImGui::SliderFloat("Coverage", &config_.coverage, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Density", &config_.density, 0.0F, 0.08F, "%.3f");
        ImGui::SliderFloat("Wind", &config_.wind_speed_mps, 0.0F, 900.0F, "%.0f m/s");
        ImGui::SliderFloat("Weather scale", &config_.weather_scale_km, 40.0F, 500.0F, "%.0f km");
        if (ImGui::CollapsingHeader("Sampling", ImGuiTreeNodeFlags_DefaultOpen)) {
            draw_enum_combo("Sampling mode", config_.sampling_mode, kCloudSamplingModes,
                            clouds_sampling_mode_name);
            ImGui::SliderFloat("Jitter", &config_.jitter_strength, 0.0F, 1.0F, "%.2f");
        }
        if (ImGui::CollapsingHeader("Shape / Density", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Crispiness", &config_.crispiness, 1.0F, 120.0F, "%.1f");
            ImGui::SliderFloat("Curliness", &config_.curliness, 0.01F, 3.0F, "%.2f");
            ImGui::SliderFloat("Vertical shear", &config_.vertical_shear_fraction, 0.0F, 0.5F,
                               "%.2f");
            ImGui::SliderFloat("Detail erosion", &config_.detail_erosion, 0.0F, 1.0F, "%.2f");
            ImGui::Checkbox("Powder", &config_.powder_enabled);
        }
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Ambient", &config_.ambient_strength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Direct", &config_.direct_strength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Phase / rim", &config_.phase_strength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Absorption", &config_.absorption, 0.0F, 1.5F, "%.2f");
            ImGui::SliderFloat("Shadow strength", &config_.shadow_strength, 0.0F, 2.0F, "%.2f");
            ImGui::SliderFloat("Horizon fill", &config_.horizon_strength, 0.0F, 2.0F, "%.2f");
        }
        if (ImGui::CollapsingHeader("Final Resolve", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Resolve", &config_.resolve_strength, 0.0F, 1.0F, "%.2f");
            ImGui::SliderFloat("Contrast", &config_.final_contrast, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Saturation", &config_.final_saturation, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Horizon glow", &config_.horizon_glow_strength, 0.0F, 3.0F,
                               "%.2f");
            ImGui::SliderFloat("Sun glare", &config_.sun_glare_strength, 0.0F, 3.0F, "%.2f");
        }
        ImGui::Separator();
        ImGui::Text("FPS: %.1f / %.2f ms", latest_fps_, latest_frame_ms_);
        ImGui::Text("Base noise: %u^3", kBaseNoiseSize);
        ImGui::Text("Detail noise: %u^3", kDetailNoiseSize);
        ImGui::Text("Weather texture: %u x %u", kWeatherTextureSize, kWeatherTextureSize);
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
                        pitch_ = cloud_clamp_pitch(value, 0.0F);
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
        if (std::find(kCloudCameraModes.begin(), kCloudCameraModes.end(),
                      config_.camera_mode) == kCloudCameraModes.end()) {
            config_.camera_mode = CloudsCameraMode::Surface;
        }
        config_.camera_altitude_m = clouds_default_camera_altitude_m(config_.camera_mode);
        yaw_ = 0.0F;
        pitch_ = cloud_clamp_pitch(config_.camera_mode, 0.0F);
        elapsed_seconds_ = 0.0F;
    }

    void create_global_resources(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu) {
        if (base_noise_.has_value()) {
            return;
        }

        base_noise_.emplace(device, cloud_volume_texture_config(kBaseNoiseSize));
        detail_noise_.emplace(device, cloud_volume_texture_config(kDetailNoiseSize));
        weather_texture_.emplace(device, cloud_weather_texture_config());

        generate_storage_volume_texture(
            device, gpu, "cloud generate base noise",
            shader_path("cloud_perlin_worley.comp.spv"), base_noise_.value(),
            cubey::render::ceil_dispatch_groups(kBaseNoiseSize, kBaseNoiseSize, kBaseNoiseSize,
                                                kCloudVolumeGroupSize));
        generate_storage_volume_texture(
            device, gpu, "cloud generate detail noise", shader_path("cloud_worley.comp.spv"),
            detail_noise_.value(),
            cubey::render::ceil_dispatch_groups(kDetailNoiseSize, kDetailNoiseSize,
                                                kDetailNoiseSize, kCloudVolumeGroupSize));
        generate_storage_texture(
            device, gpu, "cloud generate weather", shader_path("cloud_weather.comp.spv"),
            weather_texture_->handle(), weather_texture_->view(),
            cubey::render::ceil_dispatch_groups(kWeatherTextureSize, kWeatherTextureSize,
                                                kCloudComputeGroupSize));
    }

    void destroy_global_resources() {
        weather_texture_.reset();
        detail_noise_.reset();
        base_noise_.reset();
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        composite_sampler_.reset();
        march_material_.reset();
        composite_material_.reset();
        march_pipeline_.reset();
        composite_pipeline_.reset();
        frame_uniforms_.reset();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count) {
        graph_executor_.resize(frame_slot_count);
        frame_uniforms_.emplace(device, frame_slot_count);

        const cubey::render::MaterialPassInfo march_pass = cloud_march_pass_info();
        march_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                            .material_pass = march_pass,
                                            .descriptor_set = 0,
                                            .set_count = frame_slot_count,
                                        });
        const std::array<VkDescriptorSetLayout, 1> march_layouts{march_material_->layout()};
        march_pipeline_.emplace(device, cubey::render::ComputePipelineResourceConfig{
                                            .shader_stage = cubey::render::compute_shader_file(
                                                shader_path("cloud_march.comp.spv")),
                                            .descriptor_set_layouts = march_layouts,
                                        });

        const cubey::render::MaterialPassInfo composite_pass = cloud_composite_pass_info();
        composite_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                                .material_pass = composite_pass,
                                                .descriptor_set = 0,
                                                .set_count = frame_slot_count,
                                            });
        composite_sampler_.emplace(device, cubey::vulkan::SamplerConfig{
                                               .min_filter = VK_FILTER_LINEAR,
                                               .mag_filter = VK_FILTER_LINEAR,
                                               .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                           });
        const std::array<VkDescriptorSetLayout, 1> composite_layouts{
            composite_material_->layout()};
        const std::array<cubey::render::ShaderStageFile, 2> composite_shaders{
            cubey::render::vertex_shader_file(shader_path("cloud.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("cloud_composite.frag.spv")),
        };
        composite_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                                .extent = extent,
                                                .color_format = color_format,
                                                .shader_stage_files = composite_shaders,
                                                .descriptor_set_layouts = composite_layouts,
                                                .material_pass = composite_pass,
                                            });
    }

    [[nodiscard]] const cubey::render::Texture3D& base_noise() const {
        if (!base_noise_.has_value()) {
            throw std::runtime_error("cloud base noise texture is not initialized");
        }
        return base_noise_.value();
    }

    [[nodiscard]] const cubey::render::Texture3D& detail_noise() const {
        if (!detail_noise_.has_value()) {
            throw std::runtime_error("cloud detail noise texture is not initialized");
        }
        return detail_noise_.value();
    }

    [[nodiscard]] const cubey::render::Texture2D& weather_texture() const {
        if (!weather_texture_.has_value()) {
            throw std::runtime_error("cloud weather texture is not initialized");
        }
        return weather_texture_.value();
    }

    [[nodiscard]] const cubey::vulkan::Sampler& composite_sampler() const {
        if (!composite_sampler_.has_value()) {
            throw std::runtime_error("cloud composite sampler is not initialized");
        }
        return composite_sampler_.value();
    }

    [[nodiscard]] const cubey::render::FrameUniformBuffer<CloudFrameUniforms>&
    frame_uniforms() const {
        if (!frame_uniforms_.has_value()) {
            throw std::runtime_error("cloud uniforms are not initialized");
        }
        return frame_uniforms_.value();
    }

    [[nodiscard]] const cubey::render::ComputePipelineResource& march_pipeline() const {
        if (!march_pipeline_.has_value()) {
            throw std::runtime_error("cloud march pipeline is not initialized");
        }
        return march_pipeline_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& composite_pipeline() const {
        if (!composite_pipeline_.has_value()) {
            throw std::runtime_error("cloud composite pipeline is not initialized");
        }
        return composite_pipeline_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& march_material() const {
        if (!march_material_.has_value()) {
            throw std::runtime_error("cloud march material is not initialized");
        }
        return march_material_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& composite_material() const {
        if (!composite_material_.has_value()) {
            throw std::runtime_error("cloud composite material is not initialized");
        }
        return composite_material_.value();
    }

    void upload_uniforms(cubey::render::FrameSlot frame_slot, VkExtent2D target_extent) const {
        frame_uniforms().upload(
            frame_slot,
            cloud_frame_uniforms(config_, target_extent, yaw_, pitch_, elapsed_seconds_));
    }

    void record_cloud_dispatch(const cubey::vulkan::CommandRecorder& recorder,
                               VkDescriptorSet descriptor_set, VkExtent2D extent) const {
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                march_pipeline(), descriptor_set,
                cubey::render::ceil_dispatch_groups(extent.width, extent.height,
                                                    kCloudComputeGroupSize)));
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

    [[nodiscard]] CloudFrameGraph build_frame_graph(cubey::render::ColorTargetView target,
                                                       cubey::render::FrameSlot frame_slot,
                                                       CloudTargetMode mode) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer =
            graph.import_color_target("backbuffer", target, cloud_target_initial_state(mode),
                                      cloud_target_final_state(mode));
        const VkExtent2D cloud_extent = cloud_product_extent(target.extent, config_.quality);
        const cubey::render::RenderGraphTextureHandle cloud_product =
            graph.create_texture(cloud_color_texture_desc("cloud product", cloud_extent));
        const cubey::render::RenderGraphTextureHandle cloud_metadata =
            graph.create_texture(cloud_color_texture_desc("cloud metadata", cloud_extent));
        graph.add_pass("cloud march", cubey::render::RenderGraphQueueDomain::Compute)
            .write_storage_texture(cloud_product, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .write_storage_texture(cloud_metadata, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
            .execute([this, cloud_product, cloud_metadata, frame_slot, cloud_extent](
                         const cubey::render::RenderGraphExecutionContext& context) {
                static_cast<void>(cloud_product);
                static_cast<void>(cloud_metadata);
                record_cloud_dispatch(context.recorder(), march_material().set(frame_slot),
                                      cloud_extent);
            });
        graph.add_pass("cloud composite", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(cloud_product)
            .read_texture(cloud_metadata)
            .write_color(backbuffer)
            .execute([this, backbuffer, frame_slot](
                         const cubey::render::RenderGraphExecutionContext& context) {
                record_composite_draw(context.recorder(),
                                      cubey::render::resolved_color_target_view(context, backbuffer),
                                      frame_slot);
            });
        return {
            .graph = graph.compile(),
            .cloud_product = cloud_product,
            .cloud_metadata = cloud_metadata,
        };
    }

    void record_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                       const cubey::render::ColorTargetView& target,
                       cubey::render::FrameSlot frame_slot, CloudTargetMode mode,
                       cubey::render::RenderGraphCommandBufferMode command_buffer_mode,
                       const char* label) {
        upload_uniforms(frame_slot, target.extent);
        const CloudFrameGraph frame_graph = build_frame_graph(target, frame_slot, mode);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = label,
                .command_buffer_mode = command_buffer_mode,
            },
            frame_graph.graph,
            [this, &device, frame_slot,
             &frame_graph](const cubey::render::RenderGraphResourceSet& graph_resources) {
                const cubey::render::RenderGraphSampledTextureView cloud_product =
                    cubey::render::resolved_sampled_texture_view(
                        frame_graph.graph, graph_resources, frame_graph.cloud_product);
                const cubey::render::RenderGraphSampledTextureView cloud_metadata =
                    cubey::render::resolved_sampled_texture_view(
                        frame_graph.graph, graph_resources, frame_graph.cloud_metadata);
                cubey::render::MaterialDescriptorWriter(march_material().set(frame_slot))
                    .uniform_buffer(kCloudUniformBinding,
                                    frame_uniforms().buffer(frame_slot).handle(),
                                    frame_uniforms().range())
                    .storage_image(kCloudOutputBinding, cloud_product.view,
                                   VK_IMAGE_LAYOUT_GENERAL)
                    .combined_image_sampler(kCloudBaseNoiseBinding,
                                            base_noise().sampler().handle(), base_noise().view())
                    .combined_image_sampler(kCloudDetailNoiseBinding,
                                            detail_noise().sampler().handle(),
                                            detail_noise().view())
                    .combined_image_sampler(kCloudWeatherBinding,
                                            weather_texture().sampler().handle(),
                                            weather_texture().view())
                    .storage_image(kCloudMetadataBinding, cloud_metadata.view,
                                   VK_IMAGE_LAYOUT_GENERAL)
                    .update(device);
                cubey::render::MaterialDescriptorWriter(composite_material().set(frame_slot))
                    .uniform_buffer(kCloudUniformBinding,
                                    frame_uniforms().buffer(frame_slot).handle(),
                                    frame_uniforms().range())
                    .combined_image_sampler(kCloudCompositeCloudBinding,
                                            composite_sampler().handle(), cloud_product.view,
                                            cloud_product.layout)
                    .combined_image_sampler(kCloudCompositeMetadataBinding,
                                            composite_sampler().handle(), cloud_metadata.view,
                                            cloud_metadata.layout)
                    .update(device);
            });
    }

    void record_windowed_frame(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                               const cubey::render::ColorTargetView& target,
                               cubey::render::FrameSlot frame_slot) {
        record_target(device, command_buffer, target, frame_slot, CloudTargetMode::Present,
                      cubey::render::RenderGraphCommandBufferMode::BeginAndEnd,
                      "vkEndCommandBuffer cloud");
    }

    void record_headless_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                                const cubey::render::ColorTargetView& target,
                                cubey::render::FrameSlot frame_slot) {
        record_target(device, command_buffer, target, frame_slot,
                      CloudTargetMode::ColorAttachment,
                      cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                      "vkEndCommandBuffer cloud headless");
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
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
    std::optional<cubey::vulkan::Sampler> composite_sampler_{};
    std::optional<cubey::render::FrameUniformBuffer<CloudFrameUniforms>> frame_uniforms_{};
    std::optional<cubey::render::MaterialInstance> march_material_{};
    std::optional<cubey::render::MaterialInstance> composite_material_{};
    std::optional<cubey::render::ComputePipelineResource> march_pipeline_{};
    std::optional<cubey::render::GraphicsPipelineResource> composite_pipeline_{};
};

} // namespace

int run_cloud(const RunConfig& config) {
    CloudApp app(config);
    return app.run();
}

} // namespace cubey::projects::cloud
