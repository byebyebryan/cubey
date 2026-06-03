#include "ocean_app.h"

#include "ocean_config.h"
#include "ocean_gpu_resources.h"
#include "ocean_mesh.h"
#include "ocean_ui.h"

#include <cubey/core/math.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/render/terrain_ocean_fields.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/memory_barriers.h>
#include <cubey/vulkan/sampler.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef CUBEY_OCEAN_SHADER_DIR
#error "CUBEY_OCEAN_SHADER_DIR must be defined by the ocean CMake target"
#endif

namespace cubey::projects::ocean {
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kCameraDistance = 125.0F;
constexpr float kCameraMinDistance = 18.0F;
constexpr float kCameraMaxDistance = 900.0F;
constexpr float kCameraBaseYaw = cubey::render::kAtmosphereEnvironmentSunriseViewYawRadians;
constexpr float kCameraBasePitch = cubey::render::kAtmosphereEnvironmentSunriseViewPitchRadians;
constexpr float kCameraNearPlane = 0.25F;
constexpr float kCameraFarPlane = 14000.0F;
constexpr VkFormat kOceanSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kOceanDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr float kGravity = 9.81F;
constexpr float kOceanSunElevationDegrees = 20.0F;
constexpr float kOceanSunAzimuthDegrees = -20.0F;

struct OceanPushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 mesh_options;
    cubey::math::Vec4 patch_bounds;
    cubey::math::Vec4 sun_direction;
    cubey::math::Vec4 debug_options;
    cubey::math::Vec4 inspection_options;
    cubey::math::Vec4 tile_lengths;
    cubey::math::Vec4 displacement_scales;
    cubey::math::Vec4 normal_scales;
    cubey::math::Vec4 cascade4_options;
    cubey::math::Vec4 water_color;
    cubey::math::Vec4 foam_color;
};

struct OceanSpectrumPushConstants {
    cubey::math::Vec4 seed_tile;
    cubey::math::Vec4 spectrum_options;
    cubey::math::Vec4 shape_options;
    cubey::math::Vec4 cascade_options;
};

struct OceanTerrainFieldUniforms {
    cubey::math::Vec4 uv_transform;
    cubey::math::Vec4 ranges_flags;
};

struct OceanModulatePushConstants {
    cubey::math::Vec4 tile_depth_time;
    cubey::math::Vec4 cascade_options;
};

struct OceanFftPushConstants {
    cubey::math::Vec4 fft_options;
    cubey::math::Vec4 pass_options;
};

struct OceanUnpackPushConstants {
    cubey::math::Vec4 foam_options;
    cubey::math::Vec4 cascade_options;
};

static_assert(sizeof(OceanPushConstants) == sizeof(float) * 64U);
static_assert(sizeof(OceanSpectrumPushConstants) == sizeof(float) * 16U);
static_assert(sizeof(OceanTerrainFieldUniforms) == sizeof(float) * 8U);
static_assert(sizeof(OceanModulatePushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanFftPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanUnpackPushConstants) == sizeof(float) * 8U);

enum class OceanRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

struct OceanFrameGraph {
    cubey::render::CompiledRenderGraph graph;
    cubey::render::RenderGraphTextureHandle backbuffer{};
    cubey::render::RenderGraphTextureHandle scene_color{};
    cubey::render::RenderGraphTextureHandle surface_depth{};
};

struct OceanCameraPresetConfig {
    OceanCameraPreset preset = OceanCameraPreset::Default;
    float distance = kCameraDistance;
    float yaw = kCameraBaseYaw;
    float pitch = kCameraBasePitch;
};

[[nodiscard]] float radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
ocean_atmosphere_run_state(const RunConfig& run_config) {
    return cubey::atmosphere_environment_run_state_from_config(
        run_config.atmosphere,
        {
            .sun_elevation_degrees = kOceanSunElevationDegrees,
            .sun_azimuth_degrees = kOceanSunAzimuthDegrees,
            .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnly,
            .reference_geometry_enabled = false,
        });
}

[[nodiscard]] float jonswap_alpha(float wind_speed, float fetch_length_m) {
    return 0.076F *
           std::pow((wind_speed * wind_speed) / std::max(fetch_length_m * kGravity, 0.001F), 0.22F);
}

[[nodiscard]] float jonswap_peak_frequency(float wind_speed, float fetch_length_m) {
    return 22.0F * std::pow((kGravity * kGravity) / std::max(wind_speed * fetch_length_m, 0.001F),
                            1.0F / 3.0F);
}

[[nodiscard]] std::uint32_t log2_exact(std::uint32_t value) {
    if (!ocean_is_power_of_two(value)) {
        throw std::runtime_error("ocean FFT resolution must be a power of two");
    }
    std::uint32_t result = 0;
    while (value > 1U) {
        value >>= 1U;
        ++result;
    }
    return result;
}

[[nodiscard]] std::uint32_t triangle_count(const OceanConfig& config) {
    return ocean_mesh_total_triangle_count(config);
}

[[nodiscard]] cubey::render::ComputeDispatchGroups
ocean_dispatch_groups(const OceanConfig& config) {
    return cubey::render::ceil_dispatch_groups(config.map_size, config.map_size, 16U);
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
ocean_depth_texture_desc(const char* label, VkExtent2D extent, VkFormat format) {
    return {
        .label = label,
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

[[nodiscard]] OceanCameraPresetConfig ocean_camera_preset_config(OceanCameraPreset preset) {
    switch (preset) {
    case OceanCameraPreset::Default:
        return {.preset = preset,
                .distance = kCameraDistance,
                .yaw = kCameraBaseYaw,
                .pitch = kCameraBasePitch};
    case OceanCameraPreset::Low:
        return {.preset = preset, .distance = 180.0F, .yaw = 0.42F, .pitch = -0.08F};
    case OceanCameraPreset::Mid:
        return {.preset = preset, .distance = 360.0F, .yaw = 0.32F, .pitch = -0.28F};
    case OceanCameraPreset::High:
        return {.preset = preset, .distance = 650.0F, .yaw = 0.24F, .pitch = -0.46F};
    case OceanCameraPreset::Close:
        return {.preset = preset, .distance = 48.0F, .yaw = 0.62F, .pitch = -0.28F};
    case OceanCameraPreset::Overhead:
        return {.preset = preset, .distance = 220.0F, .yaw = 0.20F, .pitch = -1.05F};
    case OceanCameraPreset::Wide:
        return {.preset = preset, .distance = 900.0F, .yaw = 0.20F, .pitch = -0.70F};
    }
    return {.preset = OceanCameraPreset::Default,
            .distance = kCameraDistance,
            .yaw = kCameraBaseYaw,
            .pitch = kCameraBasePitch};
}

[[nodiscard]] bool ocean_resolution_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.map_size != rhs.map_size;
}

[[nodiscard]] bool ocean_terrain_field_source_changed(const OceanConfig& lhs,
                                                      const OceanConfig& rhs) {
    return lhs.mesh_extent != rhs.mesh_extent || lhs.depth != rhs.depth;
}

[[nodiscard]] bool ocean_wave_source_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.depth != rhs.depth || lhs.spectral_domains_enabled != rhs.spectral_domains_enabled ||
           lhs.cascades != rhs.cascades || lhs.cascade_enabled != rhs.cascade_enabled;
}

[[nodiscard]] std::uint32_t ocean_enabled_cascade_mask(const OceanConfig& config) {
    std::uint32_t mask = 0U;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (ocean_cascade_enabled(config, cascade)) {
            mask |= 1U << cascade;
        }
    }
    return mask;
}

[[nodiscard]] OceanTerrainFieldUniforms
ocean_terrain_field_uniforms(const cubey::render::TerrainOceanPackedFields& fields, bool enabled) {
    const cubey::render::TerrainOceanGridDesc& desc = fields.desc;
    const float span_x =
        std::max(static_cast<float>(desc.width > 1U ? desc.width - 1U : 1U) * desc.cell_size_m,
                 desc.cell_size_m);
    const float span_z =
        std::max(static_cast<float>(desc.height > 1U ? desc.height - 1U : 1U) * desc.cell_size_m,
                 desc.cell_size_m);
    return {
        .uv_transform =
            {
                desc.origin_x_m - span_x * 0.5F,
                desc.origin_z_m - span_z * 0.5F,
                1.0F / span_x,
                1.0F / span_z,
            },
        .ranges_flags =
            {
                fields.max_water_depth_m,
                fields.max_abs_shore_sdf_m,
                fields.max_slope,
                enabled ? 1.0F : 0.0F,
            },
    };
}

[[nodiscard]] cubey::render::TerrainOceanPackedFields
make_ocean_diagnostic_terrain_fields(const OceanConfig& config) {
    constexpr std::uint32_t field_extent = 129U;
    const float cell_size = (config.mesh_extent * 2.0F) / static_cast<float>(field_extent - 1U);
    cubey::render::TerrainOceanFieldView field_view;
    field_view.desc = {
        .version = 1,
        .seed = 0x4f6365616eULL,
        .width = field_extent,
        .height = field_extent,
        .cell_size_m = cell_size,
        .sea_level_m = 0.0F,
        .origin_x_m = 0.0F,
        .origin_z_m = 0.0F,
    };

    const std::size_t count = cubey::render::terrain_ocean_sample_count(field_view.desc);
    std::vector<float> height(count, 0.0F);
    std::vector<float> water_depth(count, 0.0F);
    std::vector<float> shore_sdf(count, 0.0F);
    std::vector<float> slope(count, 0.0F);
    std::vector<cubey::render::TerrainOceanMaterialMask> material_masks(count);

    const float island_radius = config.mesh_extent * 0.22F;
    const float shoal_radius = config.mesh_extent * 0.42F;
    const float minimum_depth = std::max(config.depth * 0.10F, 0.75F);
    const float deep_depth = std::max(config.depth, minimum_depth + 1.0F);
    for (std::uint32_t y = 0; y < field_view.desc.height; ++y) {
        const float z =
            (static_cast<float>(y) - static_cast<float>(field_view.desc.height - 1U) * 0.5F) *
            cell_size;
        for (std::uint32_t x = 0; x < field_view.desc.width; ++x) {
            const float world_x =
                (static_cast<float>(x) - static_cast<float>(field_view.desc.width - 1U) * 0.5F) *
                cell_size;
            const std::size_t index =
                static_cast<std::size_t>(y) * field_view.desc.width + static_cast<std::size_t>(x);
            const float radius = std::sqrt(world_x * world_x + z * z);
            const float signed_shore = island_radius - radius;
            const float shoal = 1.0F - std::clamp((radius - island_radius) /
                                                      std::max(shoal_radius - island_radius, 1.0F),
                                                  0.0F, 1.0F);
            const float depth = std::max(minimum_depth, deep_depth * (1.0F - shoal * 0.82F));

            shore_sdf[index] = signed_shore;
            water_depth[index] = signed_shore > 0.0F ? 0.0F : depth;
            height[index] = signed_shore > 0.0F ? signed_shore * 0.035F : -depth;
            slope[index] = std::clamp(shoal * 0.75F + std::abs(signed_shore) /
                                                          std::max(config.mesh_extent, 1.0F),
                                      0.0F, 1.0F);
            const float sand = std::clamp(shoal, 0.0F, 1.0F);
            const float rock = 0.15F;
            const float vegetation = signed_shore > 0.0F ? 0.35F : 0.0F;
            const float sediment = 0.50F;
            const float material_sum = sand + rock + vegetation + sediment;
            material_masks[index] = {
                .sand = sand / material_sum,
                .rock = rock / material_sum,
                .vegetation = vegetation / material_sum,
                .sediment = sediment / material_sum,
            };
        }
    }

    field_view.height_m = std::span<const float>(height.data(), height.size());
    field_view.water_depth_m = std::span<const float>(water_depth.data(), water_depth.size());
    field_view.shore_sdf_m = std::span<const float>(shore_sdf.data(), shore_sdf.size());
    field_view.slope = std::span<const float>(slope.data(), slope.size());
    field_view.material_masks = std::span<const cubey::render::TerrainOceanMaterialMask>(
        material_masks.data(), material_masks.size());
    return cubey::render::pack_terrain_ocean_fields(field_view);
}

class OceanApp {
  public:
    explicit OceanApp(RunConfig config)
        : config_(std::move(config)), ocean_config_(ocean_config_from_run_config(config_)),
          atmosphere_state_(ocean_atmosphere_run_state(config_)),
          atmosphere_lighting_(
              cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment)),
          render_view_(ocean_config_.render_view) {
        diagnostics_.selected_cascade = config_.ocean.cascade;
        diagnostics_.wire_overlay = config_.ocean.wire_overlay;
        if (cubey::run_config_float_is_set(config_.ocean.wire_opacity)) {
            diagnostics_.wire_opacity = config_.ocean.wire_opacity;
        }
        camera_.set_projection(camera_.fovy_radians(), kCameraNearPlane, kCameraFarPlane);
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_distance_limits(kCameraMinDistance, kCameraMaxDistance);
        orbit_controller_.set_auto_rotation_speed(0.0F);
        apply_camera_preset(camera_preset_);
    }

    OceanApp(const OceanApp&) = delete;
    OceanApp& operator=(const OceanApp&) = delete;

    ~OceanApp() {
        destroy_swapchain_resources();
    }

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
            create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_windowed(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) {
            bool atmosphere_changed = false;
            draw_ocean_ui({
                .config = ocean_config_,
                .diagnostics = diagnostics_,
                .atmosphere = atmosphere_state_,
                .performance =
                    {
                        .frame_stats = latest_frame_stats_,
                        .latest_fps = latest_fps_,
                        .latest_frame_ms = latest_frame_ms_,
                        .process = process_stats_.sample(),
                        .device_memory_budget = context.device().device_memory_budget(),
                    },
                .render_view = render_view_,
                .camera_preset = camera_preset_,
                .paused = paused_,
                .reset_requested = reset_requested_,
                .step_requested = step_requested_,
                .camera_preset_requested = camera_preset_requested_,
                .atmosphere_changed = atmosphere_changed,
            });
            if (camera_preset_requested_) {
                apply_camera_preset(camera_preset_);
                camera_preset_requested_ = false;
            }
            if (atmosphere_changed) {
                refresh_atmosphere_lighting();
            }
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context.device(), frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "ocean",
                .ready_status = "rendering ocean project",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_pipeline(context.device(), context.gpu(), target.format, target.extent,
                            cubey::host::headless_capture_frame_slot_count(config_));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            time_seconds_ = frame.timing.elapsed_seconds;
            last_delta_seconds_ =
                frame.timing.delta_seconds > 0.0 ? frame.timing.delta_seconds : (1.0 / 60.0);
            update_atmosphere_time(frame.timing.delta_seconds);
            record_ocean_target(command_buffer, context.device(), target, frame.frame_slot,
                                OceanRenderTargetMode::ColorAttachment);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_windowed(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::Space)) {
            paused_ = !paused_;
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_requested_ = true;
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            render_view_ = next_ocean_render_view(render_view_);
        }

        orbit_controller_.update_pointer_input(input, timing.delta_seconds);
        if (reset_requested_) {
            time_seconds_ = 0.0;
            foam_initialized_ = false;
            apply_camera_preset(camera_preset_);
            reset_requested_ = false;
        }
        if (paused_ && step_requested_) {
            time_seconds_ += 1.0 / 60.0;
            step_requested_ = false;
        } else if (!paused_) {
            time_seconds_ += timing.delta_seconds;
            step_requested_ = false;
        }
        last_delta_seconds_ = timing.delta_seconds > 0.0 ? timing.delta_seconds : (1.0 / 60.0);
        update_atmosphere_time(timing.delta_seconds);
        sync_gpu_resources(context);
    }

    void refresh_atmosphere_lighting() {
        atmosphere_lighting_ =
            cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment);
        if (atmosphere_runtime_.resources_created()) {
            atmosphere_runtime_.set_environment(atmosphere_state_.environment);
        }
    }

    void update_atmosphere_time(double delta_seconds) {
        if (cubey::atmosphere_environment_advance_time(atmosphere_state_, delta_seconds)) {
            refresh_atmosphere_lighting();
        }
    }

    std::optional<FrameStatsSample> record_frame_stats(VkExtent2D extent,
                                                       const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = triangle_count(ocean_config_),
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void create_pipeline(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                         VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count = 1U) {
        create_atmosphere_environment_runtime(device, gpu, frame_slot_count);
        ocean_gpu_.create(device, OceanGpuResourceConfig{
                                      .ocean = ocean_config_,
                                      .shader_dir = CUBEY_OCEAN_SHADER_DIR,
                                      .color_format = kOceanSceneColorFormat,
                                      .depth_format = kOceanDepthFormat,
                                      .target_extent = extent,
                                      .frame_slot_count = frame_slot_count,
                                  });
        create_terrain_ocean_field_resources(device, gpu, frame_slot_count);
        ocean_gpu_.update_terrain_ocean_field_descriptor(device,
                                                         terrain_ocean_fields_texture_.value());
        update_terrain_ocean_field_uniform_descriptors(device);
        const cubey::render::AtmosphereReflectionProbe& atmosphere_probe =
            atmosphere_runtime_.reflection_probe();
        ocean_gpu_.update_atmosphere_probe_descriptors(device, atmosphere_probe.prefiltered_cube(),
                                                       atmosphere_probe.sky_radiance_cube());
        create_atmosphere_background_frame(device, extent, frame_slot_count);
        hdr_post_frame_.create_materials(device, {
                                                     .frame_slot_count = frame_slot_count,
                                                 });
        const std::array<cubey::render::ShaderStageFile, 2> post_shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = std::filesystem::path(CUBEY_OCEAN_SHADER_DIR) / "forward_pbr_post.vert.spv",
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = std::filesystem::path(CUBEY_OCEAN_SHADER_DIR) / "forward_pbr_post.frag.spv",
            },
        };
        hdr_post_frame_.create_pipeline(device, {
                                                    .extent = extent,
                                                    .color_format = color_format,
                                                    .shader_stage_files = post_shader_stage_files,
                                                });
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
        pipeline_color_format_ = color_format;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        gpu_config_ = ocean_config_;
    }

    void create_terrain_ocean_field_resources(cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu,
                                              std::uint32_t frame_slot_count) {
        terrain_ocean_fields_ = make_ocean_diagnostic_terrain_fields(ocean_config_);
        terrain_ocean_fields_texture_.emplace(
            cubey::render::create_uploaded_terrain_ocean_field_texture(device, gpu,
                                                                       terrain_ocean_fields_));
        terrain_ocean_field_uniforms_.emplace(device, frame_slot_count);
    }

    void upload_terrain_ocean_field_uniform(cubey::render::FrameSlot frame_slot) const {
        if (!terrain_ocean_field_uniforms_.has_value()) {
            throw std::runtime_error("ocean terrain field uniforms are not initialized");
        }
        terrain_ocean_field_uniforms_->upload(
            frame_slot, ocean_terrain_field_uniforms(terrain_ocean_fields_,
                                                     ocean_config_.terrain_fields_enabled));
    }

    void update_terrain_ocean_field_uniform_descriptors(const cubey::vulkan::Device& device) {
        if (!terrain_ocean_field_uniforms_.has_value()) {
            throw std::runtime_error("ocean terrain field uniforms are not initialized");
        }
        const std::uint32_t slot_count = terrain_ocean_field_uniforms_->slot_count();
        for (std::uint32_t index = 0; index < slot_count; ++index) {
            const cubey::render::FrameSlot frame_slot{.index = index, .count = slot_count};
            upload_terrain_ocean_field_uniform(frame_slot);
            ocean_gpu_.update_terrain_ocean_field_uniform_descriptor(
                device, frame_slot, terrain_ocean_field_uniforms_->buffer(frame_slot).handle(),
                terrain_ocean_field_uniforms_->range());
        }
    }

    void create_atmosphere_environment_runtime(cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu,
                                               std::uint32_t frame_slot_count) {
        if (!atmosphere_background_atlases_.has_value()) {
            atmosphere_background_atlases_.emplace(
                cubey::render::create_atmosphere_background_generated_textures(
                    device, gpu,
                    {
                        .lunar_extent = 128,
                        .night_sky_extent = 128,
                    }));
        }
        if (!atmosphere_runtime_.resources_created()) {
            atmosphere_runtime_.create_resources(
                device, cubey::AtmosphereEnvironmentRuntimeResourceConfig{
                            .reflection_extent = 64,
                            .reflection_mip_levels = 5,
                            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                            .frame_slot_count = frame_slot_count,
                            .atmosphere_textures = atmosphere_background_atlases_->bindings(),
                        });
            const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
            atmosphere_runtime_.create_pipelines(
                device,
                cubey::AtmosphereEnvironmentRuntimePipelineConfig{
                    .atmosphere_vertex_shader = shader_dir / "atmosphere.vert.spv",
                    .atmosphere_fragment_shader = shader_dir / "atmosphere.frag.spv",
                    .reflection_prefilter_vertex_shader = shader_dir / "atmosphere.vert.spv",
                    .reflection_prefilter_fragment_shader =
                        shader_dir / "atmosphere_reflection_prefilter.frag.spv",
                });
            atmosphere_runtime_.mark_full_update_pending();
        }
        atmosphere_runtime_.set_environment(atmosphere_state_.environment);
    }

    void create_atmosphere_background_frame(const cubey::vulkan::Device& device, VkExtent2D extent,
                                            std::uint32_t frame_slot_count) {
        if (!atmosphere_background_atlases_.has_value()) {
            throw std::runtime_error("ocean atmosphere background textures are not initialized");
        }
        atmosphere_background_.destroy();
        atmosphere_background_.create_materials(
            device, cubey::render::AtmosphereBackgroundFrameMaterialConfig{
                        .frame_slot_count = frame_slot_count,
                        .textures = atmosphere_background_atlases_->bindings(),
                    });
        const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_dir / "atmosphere.vert.spv"),
            cubey::render::fragment_shader_file(shader_dir / "atmosphere.frag.spv"),
        };
        atmosphere_background_.create_pipeline(
            device, cubey::render::AtmosphereBackgroundFramePipelineConfig{
                        .extent = extent,
                        .color_format = kOceanSceneColorFormat,
                        .depth_format = kOceanDepthFormat,
                        .shader_stage_files = shaders,
                    });
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        hdr_post_frame_.destroy();
        atmosphere_background_.destroy();
        ocean_gpu_.reset();
        atmosphere_runtime_.destroy();
        terrain_ocean_fields_texture_.reset();
        terrain_ocean_field_uniforms_.reset();
        terrain_ocean_fields_ = {};
        atmosphere_background_atlases_.reset();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        gpu_config_.reset();
    }

    void sync_gpu_resources(cubey::host::WindowedAppContext& context) {
        validate_ocean_config(ocean_config_);
        if (!gpu_config_.has_value()) {
            create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
            return;
        }
        if (ocean_resolution_changed(ocean_config_, gpu_config_.value())) {
            cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                                 "vkDeviceWaitIdle before ocean resource recreation");
            create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
            return;
        }
        if (ocean_terrain_field_source_changed(ocean_config_, gpu_config_.value())) {
            cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                                 "vkDeviceWaitIdle before ocean terrain field recreation");
            terrain_ocean_fields_texture_.reset();
            terrain_ocean_field_uniforms_.reset();
            create_terrain_ocean_field_resources(context.device(), context.gpu(),
                                                 context.frame_slot_count());
            ocean_gpu_.update_terrain_ocean_field_descriptor(context.device(),
                                                             terrain_ocean_fields_texture_.value());
            update_terrain_ocean_field_uniform_descriptors(context.device());
        }
        if (ocean_wave_source_changed(ocean_config_, gpu_config_.value())) {
            spectrum_initialized_ = false;
            foam_initialized_ = false;
        }
        gpu_config_ = ocean_config_;
    }

    void apply_camera_preset(OceanCameraPreset preset) {
        const OceanCameraPresetConfig config = ocean_camera_preset_config(preset);
        camera_preset_ = config.preset;
        camera_base_yaw_ = config.yaw;
        camera_base_pitch_ = config.pitch;
        orbit_controller_.set_home_distance(config.distance);
        orbit_controller_.reset();
    }

    [[nodiscard]] cubey::Transform3D camera_transform() const {
        return cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = {0.0F, 0.0F, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = camera_base_yaw_ + orbit_controller_.yaw(),
            .pitch = camera_base_pitch_ + orbit_controller_.pitch(),
        });
    }

    [[nodiscard]] OceanPushConstants surface_push_constants(VkExtent2D extent,
                                                            const OceanMeshPatch& patch) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::math::Mat4 view_projection = camera_.view_projection_matrix(transform, aspect);
        const cubey::math::Vec4 sun_direction = atmosphere_primary_light_uniform();
        const float debug_z = render_view_ == OceanRenderView::Exposure
                                  ? display_exposure()
                                  : static_cast<float>(ocean_config_.mesh_lod_levels - 1U);

        return {
            .view_projection = view_projection,
            .camera_time =
                {
                    transform.translation.x,
                    transform.translation.y,
                    transform.translation.z,
                    static_cast<float>(time_seconds_),
                },
            .mesh_options =
                {
                    static_cast<float>(patch.cells_x),
                    static_cast<float>(patch.cells_z),
                    ocean_config_.mesh_extent,
                    ocean_config_.horizon_fog,
                },
            .patch_bounds =
                {
                    patch.bounds.min_x,
                    patch.bounds.max_x,
                    patch.bounds.min_z,
                    patch.bounds.max_z,
                },
            .sun_direction = sun_direction,
            .debug_options =
                {
                    static_cast<float>(static_cast<std::uint32_t>(render_view_)),
                    static_cast<float>(patch.level),
                    debug_z,
                    diagnostics_.wire_overlay ? std::clamp(diagnostics_.wire_opacity, 0.0F, 1.0F)
                                              : 0.0F,
                },
            .inspection_options =
                {
                    static_cast<float>(diagnostics_.selected_cascade),
                    diagnostics_.shape_anti_repeat_strength,
                    ocean_config_.foam_density,
                    ocean_config_.foam_sharpness,
                },
            .tile_lengths =
                {
                    ocean_config_.cascades[0].tile_length,
                    ocean_config_.cascades[1].tile_length,
                    ocean_config_.cascades[2].tile_length,
                    ocean_config_.cascades[3].tile_length,
                },
            .displacement_scales =
                {
                    ocean_config_.cascades[0].displacement_scale,
                    ocean_config_.cascades[1].displacement_scale,
                    ocean_config_.cascades[2].displacement_scale,
                    ocean_config_.cascades[3].displacement_scale,
                },
            .normal_scales =
                {
                    ocean_config_.cascades[0].normal_scale,
                    ocean_config_.cascades[1].normal_scale,
                    ocean_config_.cascades[2].normal_scale,
                    ocean_config_.cascades[3].normal_scale,
                },
            .cascade4_options =
                {
                    ocean_config_.cascades[4].tile_length,
                    ocean_config_.cascades[4].displacement_scale,
                    ocean_config_.cascades[4].normal_scale,
                    static_cast<float>(ocean_config_.map_size),
                },
            .water_color =
                {
                    ocean_config_.water_color_r,
                    ocean_config_.water_color_g,
                    ocean_config_.water_color_b,
                    ocean_config_.roughness,
                },
            .foam_color =
                {
                    ocean_config_.foam_color_r,
                    ocean_config_.foam_color_g,
                    ocean_config_.foam_color_b,
                    ocean_config_.normal_strength,
                },
        };
    }

    [[nodiscard]] OceanSurfaceFeatureUniforms surface_feature_uniforms() const {
        return {
            .feature_options =
                {
                    ocean_config_.surface_shape_strength,
                    ocean_config_.surface_foam_strength,
                    ocean_config_.foam_history_strength,
                    diagnostics_.detail_anti_repeat_strength,
                },
            .feature_options2 =
                {
                    ocean_config_.terrain_foam_strength,
                    ocean_config_.shape_fade_distance_scale,
                    ocean_config_.normal_fade_distance_scale,
                    ocean_config_.foam_fade_distance_scale,
                },
            .material_options =
                {
                    ocean_config_.atmosphere_material_strength,
                    ocean_config_.atmosphere_sky_strength,
                    ocean_config_.atmosphere_reflection_strength,
                    ocean_config_.atmosphere_light_strength,
                },
            .fade_options =
                {
                    ocean_config_.foam_lighting_strength,
                    0.0F,
                    0.0F,
                    0.0F,
                },
            .cascade_options =
                {
                    static_cast<float>(ocean_enabled_cascade_mask(ocean_config_)),
                    0.0F,
                    0.0F,
                    0.0F,
                },
        };
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_background_uniforms(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::render::ViewRayBasis3D view_rays =
            cubey::render::view_ray_basis_3d(transform.rotation, aspect, camera_.fovy_radians());
        return atmosphere_runtime_
            .frame({
                .view_rays = view_rays,
                .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
            })
            .background;
    }

    [[nodiscard]] OceanSpectrumPushConstants
    spectrum_push_constants(std::uint32_t cascade_index) const {
        const OceanCascadeConfig& cascade = ocean_cascade(ocean_config_, cascade_index);
        const OceanCascadeDomain domain = ocean_config_.spectral_domains_enabled
                                              ? ocean_cascade_domain(ocean_config_, cascade_index)
                                              : OceanCascadeDomain{};
        const float fetch_m = cascade.fetch_length_km * 1000.0F;
        return {
            .seed_tile =
                {
                    static_cast<float>(cascade.seed_x),
                    static_cast<float>(cascade.seed_y),
                    cascade.tile_length,
                    cascade.tile_length,
                },
            .spectrum_options =
                {
                    jonswap_alpha(cascade.wind_speed, fetch_m),
                    jonswap_peak_frequency(cascade.wind_speed, fetch_m),
                    cascade.wind_speed,
                    radians(cascade.wind_direction_degrees),
                },
            .shape_options =
                {
                    ocean_config_.depth,
                    cascade.swell,
                    cascade.detail,
                    cascade.spread,
                },
            .cascade_options =
                {
                    static_cast<float>(cascade_index),
                    static_cast<float>(ocean_config_.map_size),
                    domain.active ? domain.low_k : 0.0F,
                    domain.active ? domain.high_k : 0.0F,
                },
        };
    }

    [[nodiscard]] cubey::math::Vec4 atmosphere_primary_light_uniform() const {
        const cubey::math::Vec3& light = atmosphere_lighting_.primary_light_direction;
        return {light.x, light.y, light.z, atmosphere_lighting_.primary_light_intensity};
    }

    [[nodiscard]] OceanModulatePushConstants
    modulate_push_constants(std::uint32_t cascade_index) const {
        const OceanCascadeConfig& cascade = ocean_cascade(ocean_config_, cascade_index);
        return {
            .tile_depth_time =
                {
                    cascade.tile_length,
                    cascade.tile_length,
                    ocean_config_.depth,
                    static_cast<float>(time_seconds_) + cascade.time_offset,
                },
            .cascade_options =
                {
                    static_cast<float>(cascade_index),
                    static_cast<float>(ocean_config_.map_size),
                    0.0F,
                    0.0F,
                },
        };
    }

    [[nodiscard]] OceanFftPushConstants fft_push_constants(std::uint32_t stage, bool horizontal,
                                                           bool first_pass) const {
        return {
            .fft_options =
                {
                    static_cast<float>(ocean_config_.map_size),
                    static_cast<float>(stage),
                    horizontal ? 1.0F : 0.0F,
                    first_pass ? 1.0F : 0.0F,
                },
            .pass_options = {0.0F, 0.0F, 0.0F, 0.0F},
        };
    }

    [[nodiscard]] OceanUnpackPushConstants
    unpack_push_constants(std::uint32_t cascade_index) const {
        const OceanCascadeConfig& cascade = ocean_cascade(ocean_config_, cascade_index);
        const float delta_seconds =
            static_cast<float>(last_delta_seconds_ > 0.0 ? last_delta_seconds_ : (1.0 / 60.0));
        const float foam_grow_rate = delta_seconds * cascade.foam_amount * 7.5F;
        const float foam_decay_rate =
            delta_seconds * std::max(0.5F, 10.0F - cascade.foam_amount) * 1.15F;
        return {
            .foam_options =
                {
                    cascade.whitecap,
                    foam_grow_rate,
                    foam_decay_rate,
                    foam_initialized_ ? 1.0F : 0.0F,
                },
            .cascade_options =
                {
                    static_cast<float>(cascade_index),
                    static_cast<float>(ocean_config_.map_size),
                    0.0F,
                    0.0F,
                },
        };
    }

    void record_atmosphere_background(const cubey::vulkan::CommandRecorder& recorder,
                                      VkExtent2D extent,
                                      cubey::render::FrameSlot frame_slot) const {
        atmosphere_background_.upload(frame_slot, atmosphere_background_uniforms(extent));
        cubey::render::record_fullscreen_pipeline_draw(
            recorder, cubey::render::FullscreenPipelineDrawInfo{
                          .pipeline = &atmosphere_background_.pipeline(),
                          .descriptor_set = atmosphere_background_.material().set(frame_slot),
                          .descriptor_set_index = 0,
                      });
    }

    void record_ocean_draw(const cubey::vulkan::CommandRecorder& recorder, VkExtent2D extent,
                           cubey::render::FrameSlot frame_slot) const {
        const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(ocean_config_);
        const cubey::render::GraphicsPipelineResource& surface_pipeline =
            ocean_gpu_.surface_pipeline();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pipeline.pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pipeline.layout(), 0,
                                     ocean_gpu_.surface_set(frame_slot));
        ocean_gpu_.upload_surface_feature_uniforms(frame_slot, surface_feature_uniforms());
        for (const OceanMeshPatch& patch : patches) {
            const OceanPushConstants constants = surface_push_constants(extent, patch);
            recorder.push_constants(surface_pipeline.layout(),
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    constants);
            recorder.draw(ocean_mesh_patch_vertex_count(patch));
        }
    }

    [[nodiscard]] cubey::render::PbrPostUniforms post_uniforms() const {
        return cubey::render::hdr_post_uniforms(pipeline_color_format_, display_exposure());
    }

    [[nodiscard]] float display_exposure() const {
        if (atmosphere_state_.auto_exposure_enabled && !config_.pbr.exposure_explicit) {
            return atmosphere_state_.resolved_exposure;
        }
        return ocean_config_.exposure;
    }

    void record_ocean_post(const cubey::vulkan::CommandRecorder& recorder,
                           cubey::render::ColorTargetView target,
                           cubey::render::FrameSlot frame_slot) const {
        hdr_post_frame_.record_pass(recorder, target, frame_slot);
    }

    [[nodiscard]] OceanFrameGraph
    build_ocean_frame_graph(cubey::render::ColorTargetView color_target,
                            cubey::render::FrameSlot frame_slot,
                            OceanRenderTargetMode target_mode) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureState initial_state =
            target_mode == OceanRenderTargetMode::Present
                ? cubey::render::render_graph_undefined_texture_state()
                : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureState final_state =
            target_mode == OceanRenderTargetMode::Present
                ? cubey::render::render_graph_present_texture_state()
                : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureHandle backbuffer =
            graph.import_color_target("ocean backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "ocean scene color", color_target.extent, kOceanSceneColorFormat));
        const cubey::render::RenderGraphTextureHandle surface_depth =
            graph.create_texture(ocean_depth_texture_desc("ocean surface depth",
                                                          color_target.extent, kOceanDepthFormat));

        graph.add_pass("ocean scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(surface_depth)
            .execute([this, scene_color, frame_slot,
                      surface_depth](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView depth =
                    cubey::render::resolved_depth_target_view(context, surface_depth);
                cubey::render::record_render_target_pass(
                    context.recorder(), cubey::render::render_target_view(target, depth),
                    cubey::render::RenderClearValues{
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
                    [this, target,
                     frame_slot](const cubey::vulkan::CommandRecorder& draw_recorder) {
                        record_atmosphere_background(draw_recorder, target.extent, frame_slot);
                        record_ocean_draw(draw_recorder, target.extent, frame_slot);
                    });
            });
        graph.add_pass("ocean post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, color_target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_ocean_post(context.recorder(), color_target, frame_slot);
            });

        return {
            .graph = graph.compile(),
            .backbuffer = backbuffer,
            .scene_color = scene_color,
            .surface_depth = surface_depth,
        };
    }

    void update_post_descriptor(const cubey::vulkan::Device& device,
                                cubey::render::FrameSlot frame_slot,
                                const cubey::render::CompiledRenderGraph& graph,
                                const cubey::render::RenderGraphResourceSet& resources,
                                cubey::render::RenderGraphTextureHandle scene_color) const {
        hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, graph, resources,
                                                      scene_color);
    }

    void record_initial_texture_transitions(const cubey::vulkan::CommandRecorder& recorder) const {
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.h0(cascade).handle()));
            for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(
                        ocean_gpu_.field(cascade, field).handle()));
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(
                        ocean_gpu_.ping(cascade, field).handle()));
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(
                        ocean_gpu_.pong(cascade, field).handle()));
            }
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.displacement(cascade).handle()));
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.normal(cascade).handle()));
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.foam(cascade).handle()));
        }
    }

    void record_spectrum_init(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups = ocean_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_cascade_enabled(ocean_config_, cascade)) {
                continue;
            }
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.spectrum_pipeline(), ocean_gpu_.spectrum_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, spectrum_push_constants(cascade));
        }
    }

    void record_modulate(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups = ocean_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_cascade_enabled(ocean_config_, cascade)) {
                continue;
            }
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.modulate_pipeline(), ocean_gpu_.modulate_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, modulate_push_constants(cascade));
        }
    }

    void record_fft_pass(const cubey::vulkan::CommandRecorder& recorder, std::uint32_t cascade,
                         std::uint32_t field, std::uint32_t stage, bool horizontal, bool first_pass,
                         std::uint32_t descriptor_set_index) const {
        const cubey::render::ComputeDispatchGroups groups = ocean_dispatch_groups(ocean_config_);
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                ocean_gpu_.fft_pipeline(), ocean_gpu_.fft_set(cascade, field, descriptor_set_index),
                groups),
            VK_SHADER_STAGE_COMPUTE_BIT, fft_push_constants(stage, horizontal, first_pass));
    }

    void record_fft(const cubey::vulkan::CommandRecorder& recorder) const {
        const std::uint32_t stage_count = log2_exact(ocean_config_.map_size);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_cascade_enabled(ocean_config_, cascade)) {
                continue;
            }
            for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
                bool source_is_ping = true;
                for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                    const std::uint32_t set_index = stage == 1U ? 0U : (source_is_ping ? 1U : 2U);
                    record_fft_pass(recorder, cascade, field, stage, true, stage == 1U, set_index);
                    cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                    source_is_ping = stage == 1U ? true : !source_is_ping;
                }

                for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                    const std::uint32_t set_index = source_is_ping ? 1U : 2U;
                    record_fft_pass(recorder, cascade, field, stage, false, stage == 1U, set_index);
                    cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                    source_is_ping = !source_is_ping;
                }
            }
        }
    }

    void record_unpack(const cubey::vulkan::CommandRecorder& recorder) {
        const cubey::render::ComputeDispatchGroups groups = ocean_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_cascade_enabled(ocean_config_, cascade)) {
                continue;
            }
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.unpack_pipeline(), ocean_gpu_.unpack_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, unpack_push_constants(cascade));
        }
        foam_initialized_ = true;
    }

    void record_ocean_compute(const cubey::vulkan::CommandRecorder& recorder) {
        if (!textures_initialized_) {
            record_initial_texture_transitions(recorder);
            textures_initialized_ = true;
        }
        if (!spectrum_initialized_) {
            record_spectrum_init(recorder);
            cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
            spectrum_initialized_ = true;
        }
        record_modulate(recorder);
        cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
        record_fft(recorder);
        record_unpack(recorder);
        cubey::vulkan::record_shader_write_barrier(recorder.handle(),
                                                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                   VK_ACCESS_SHADER_READ_BIT);
    }

    void record_atmosphere_environment_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                                 cubey::render::FrameSlot frame_slot) {
        if (!atmosphere_runtime_.resources_created()) {
            throw std::runtime_error("ocean atmosphere runtime is not initialized");
        }
        atmosphere_runtime_.record_pending_update(recorder, frame_slot);
    }

    void record_ocean_target(VkCommandBuffer command_buffer, const cubey::vulkan::Device& device,
                             cubey::render::ColorTargetView target,
                             cubey::render::FrameSlot frame_slot,
                             OceanRenderTargetMode target_mode) {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        record_atmosphere_environment_if_needed(recorder, frame_slot);
        record_ocean_compute(recorder);
        upload_terrain_ocean_field_uniform(frame_slot);
        hdr_post_frame_.upload(frame_slot, post_uniforms());
        const OceanFrameGraph frame_graph =
            build_ocean_frame_graph(target, frame_slot, target_mode);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer ocean graph",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            frame_graph.graph,
            [this, &device, frame_slot,
             &frame_graph](const cubey::render::RenderGraphResourceSet& resources) {
                update_post_descriptor(device, frame_slot, frame_graph.graph, resources,
                                       frame_graph.scene_color);
            });
    }

    void record_windowed_frame(const cubey::vulkan::Device& device,
                               const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        record_ocean_target(frame.command_buffer, device, frame.color_target, frame.frame_slot,
                            OceanRenderTargetMode::Present);
        recorder.end("vkEndCommandBuffer ocean");
    }

    RunConfig config_;
    OceanConfig ocean_config_;
    cubey::AtmosphereEnvironmentRunState atmosphere_state_;
    cubey::render::AtmosphereEnvironmentLighting atmosphere_lighting_;
    OceanDiagnosticsConfig diagnostics_;
    OceanRenderView render_view_ = OceanRenderView::Final;
    OceanCameraPreset camera_preset_ = OceanCameraPreset::Default;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    OceanGpuResources ocean_gpu_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_{};
    cubey::render::HdrPostFrame hdr_post_frame_;
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_background_atlases_;
    cubey::AtmosphereEnvironmentRuntime atmosphere_runtime_{};
    cubey::render::TerrainOceanPackedFields terrain_ocean_fields_{};
    std::optional<cubey::render::Texture2D> terrain_ocean_fields_texture_;
    std::optional<cubey::render::FrameUniformBuffer<OceanTerrainFieldUniforms>>
        terrain_ocean_field_uniforms_;
    std::optional<OceanConfig> gpu_config_;
    VkFormat pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    double time_seconds_ = 0.0;
    double last_delta_seconds_ = 1.0 / 60.0;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    float camera_base_yaw_ = kCameraBaseYaw;
    float camera_base_pitch_ = kCameraBasePitch;
    bool paused_ = false;
    bool reset_requested_ = false;
    bool step_requested_ = false;
    bool camera_preset_requested_ = false;
    bool textures_initialized_ = false;
    bool spectrum_initialized_ = false;
    bool foam_initialized_ = false;
};

} // namespace

int run_ocean(const RunConfig& config) {
    OceanApp app(config);
    return app.run();
}

} // namespace cubey::projects::ocean
