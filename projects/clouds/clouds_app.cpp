#include "clouds_app.h"

#include "clouds_config.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/imgui_helpers.h>
#include <cubey/host/performance_ui.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
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
#include <utility>
#include <vector>

#ifndef CUBEY_CLOUDS_SHADER_DIR
#error "CUBEY_CLOUDS_SHADER_DIR must be defined by the clouds CMake target"
#endif

namespace cubey::projects::clouds {
namespace {

constexpr float kDefaultFovyRadians = 60.0F * (glm::pi<float>() / 180.0F);
constexpr float kCameraDragRadiansPerPixel = 0.006F;
constexpr float kSurfaceMinPitchRadians = -1.35F;
constexpr float kSurfaceMaxPitchRadians = 1.35F;
constexpr float kOrbitMaxLatitudeRadians = 1.30F;
constexpr VkFormat kCloudsSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kCloudsMetadataFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr float kCloudsSunIntensityScale = 0.48F;
constexpr std::array<CloudsCameraMode, 6> kCloudsCameraModes{
    CloudsCameraMode::Surface, CloudsCameraMode::SurfaceUp, CloudsCameraMode::High,
    CloudsCameraMode::HighOblique, CloudsCameraMode::Orbit, CloudsCameraMode::OrbitTerminator,
};
constexpr std::array<CloudsQuality, 3> kCloudsQualityModes{
    CloudsQuality::Quarter,
    CloudsQuality::Half,
    CloudsQuality::Full,
};
constexpr std::array<CloudsWeatherPreset, 5> kCloudsWeatherPresets{
    CloudsWeatherPreset::FairWeather,     CloudsWeatherPreset::BrokenCumulus,
    CloudsWeatherPreset::OvercastStratus, CloudsWeatherPreset::StormCells,
    CloudsWeatherPreset::HighCirrus,
};

struct CloudsPushConstants {
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward_mode;
    cubey::math::Vec4 camera_position_radius;
    cubey::math::Vec4 cloud_shell;
    cubey::math::Vec4 weather;
    cubey::math::Vec4 sun_direction_intensity;
    cubey::math::Vec4 render_options;
};

static_assert(sizeof(CloudsPushConstants) == sizeof(float) * 32U);

struct CloudsTemporalPushConstants {
    cubey::math::Vec4 options;
};

static_assert(sizeof(CloudsTemporalPushConstants) == sizeof(float) * 4U);

struct CloudsViewBasis {
    cubey::math::Vec3 position_km{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
};

enum class CloudsRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

struct CloudsFrameGraph {
    cubey::render::CompiledRenderGraph graph;
    cubey::render::RenderGraphTextureHandle cloud_color{};
    cubey::render::RenderGraphTextureHandle cloud_metadata{};
    cubey::render::RenderGraphTextureHandle cloud_history_read{};
    cubey::render::RenderGraphTextureHandle cloud_history_write{};
    cubey::render::RenderGraphTextureHandle resolved_cloud_color{};
    VkExtent2D cloud_extent{};
    bool temporal_pass_enabled = false;
    std::uint32_t history_write_index = 0;
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_CLOUDS_SHADER_DIR) / filename;
}

[[nodiscard]] VkPushConstantRange clouds_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(CloudsPushConstants),
    };
}

[[nodiscard]] VkPushConstantRange clouds_temporal_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(CloudsTemporalPushConstants),
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo clouds_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "clouds.fullscreen",
        .push_constants = {clouds_push_constant_range()},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo clouds_temporal_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "clouds.temporal",
        .descriptor_sets = {cubey::render::sampled_texture_descriptor_set_layout(0, 2)},
        .push_constants = {clouds_temporal_push_constant_range()},
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo clouds_composite_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "clouds.composite",
        .descriptor_sets = {cubey::render::sampled_texture_descriptor_set_layout(0, 2)},
        .push_constants = {clouds_push_constant_range()},
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState clouds_sampled_color_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .access_mask = VK_ACCESS_SHADER_READ_BIT,
        .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
clouds_color_texture_desc(std::string label, VkExtent2D extent, VkFormat format) {
    return {
        .label = std::move(label),
        .extent = {extent.width, extent.height, 1U},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

[[nodiscard]] bool clouds_camera_mode_is_orbit(CloudsCameraMode mode) {
    return mode == CloudsCameraMode::Orbit || mode == CloudsCameraMode::OrbitTerminator;
}

[[nodiscard]] float clouds_resolution_scale_x(CloudsQualityBudget budget) {
    return std::clamp(budget.resolution_scale, 0.05F, 1.0F);
}

[[nodiscard]] float clouds_resolution_scale_y(CloudsQualityBudget budget, CloudsCameraMode mode) {
    float scale = clouds_resolution_scale_x(budget);
    if (!clouds_camera_mode_is_orbit(mode)) {
        scale = std::max(scale, 0.5F);
    }
    return scale;
}

[[nodiscard]] VkExtent2D clouds_scaled_extent(VkExtent2D target_extent, CloudsQualityBudget budget,
                                              CloudsCameraMode mode) {
    const float scale_x = clouds_resolution_scale_x(budget);
    const float scale_y = clouds_resolution_scale_y(budget, mode);
    return {
        .width = std::max(1U, static_cast<std::uint32_t>(
                                  std::ceil(static_cast<float>(target_extent.width) * scale_x))),
        .height = std::max(1U, static_cast<std::uint32_t>(
                                   std::ceil(static_cast<float>(target_extent.height) * scale_y))),
    };
}

[[nodiscard]] std::uint64_t extent_pixel_count(VkExtent2D extent) {
    return static_cast<std::uint64_t>(extent.width) * static_cast<std::uint64_t>(extent.height);
}

[[nodiscard]] cubey::render::RenderGraphTextureState
clouds_target_initial_state(CloudsRenderTargetMode mode) {
    return mode == CloudsRenderTargetMode::Present
               ? cubey::render::render_graph_undefined_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] cubey::render::RenderGraphTextureState
clouds_target_final_state(CloudsRenderTargetMode mode) {
    return mode == CloudsRenderTargetMode::Present
               ? cubey::render::render_graph_present_texture_state()
               : cubey::render::render_graph_color_attachment_texture_state();
}

[[nodiscard]] cubey::math::Vec3 safe_normalize(cubey::math::Vec3 value,
                                               cubey::math::Vec3 fallback) {
    const float len2 = glm::dot(value, value);
    if (len2 <= 0.0000001F) {
        return fallback;
    }
    return value * glm::inversesqrt(len2);
}

[[nodiscard]] float clouds_camera_shader_mode(CloudsCameraMode mode) {
    if (clouds_camera_mode_is_orbit(mode)) {
        return 2.0F;
    }
    if (mode == CloudsCameraMode::High || mode == CloudsCameraMode::HighOblique) {
        return 1.0F;
    }
    return 0.0F;
}

[[nodiscard]] float clouds_cloud_style_shader_value(CloudsCloudStyle style) {
    return static_cast<float>(static_cast<std::uint32_t>(style));
}

[[nodiscard]] float clouds_camera_base_pitch(CloudsCameraMode mode) {
    switch (mode) {
    case CloudsCameraMode::Surface:
        return 0.22F;
    case CloudsCameraMode::SurfaceUp:
        return 0.72F;
    case CloudsCameraMode::High:
        return -0.92F;
    case CloudsCameraMode::HighOblique:
        return -0.46F;
    case CloudsCameraMode::Orbit:
    case CloudsCameraMode::OrbitTerminator:
        return 0.50F;
    }
    return 0.22F;
}

[[nodiscard]] float clouds_clamp_camera_pitch_offset(CloudsCameraMode mode, float pitch_offset) {
    const float base_pitch = clouds_camera_base_pitch(mode);
    if (clouds_camera_mode_is_orbit(mode)) {
        return std::clamp(pitch_offset, -kOrbitMaxLatitudeRadians - base_pitch,
                          kOrbitMaxLatitudeRadians - base_pitch);
    }
    return std::clamp(pitch_offset, kSurfaceMinPitchRadians - base_pitch,
                      kSurfaceMaxPitchRadians - base_pitch);
}

[[nodiscard]] CloudsViewBasis clouds_view_basis(const CloudsConfig& config, float yaw,
                                                float pitch) {
    CloudsViewBasis basis{};
    const float altitude_km = config.camera_altitude_m * 0.001F;
    basis.position_km = {0.0F, altitude_km, 0.0F};

    if (clouds_camera_mode_is_orbit(config.camera_mode)) {
        const float orbit_pitch =
            std::clamp(clouds_camera_base_pitch(config.camera_mode) + pitch,
                       -kOrbitMaxLatitudeRadians, kOrbitMaxLatitudeRadians);
        const float radius_km = config.planet_radius_m * 0.001F + altitude_km;
        const cubey::math::Vec3 planet_center{0.0F, -config.planet_radius_m * 0.001F, 0.0F};
        const cubey::math::Vec3 radial{
            std::sin(yaw) * std::cos(orbit_pitch),
            std::sin(orbit_pitch),
            std::cos(yaw) * std::cos(orbit_pitch),
        };
        basis.position_km = planet_center + radial * radius_km;
        basis.forward = safe_normalize(planet_center - basis.position_km, {0.0F, -1.0F, 0.0F});
        basis.right = safe_normalize(glm::cross(basis.forward, cubey::math::Vec3{0.0F, 1.0F, 0.0F}),
                                     {1.0F, 0.0F, 0.0F});
        basis.up = safe_normalize(glm::cross(basis.right, basis.forward), {0.0F, 0.0F, 1.0F});
        return basis;
    }

    const float base_pitch = clouds_camera_base_pitch(config.camera_mode);
    const float resolved_pitch = std::clamp(base_pitch + pitch, -1.25F, 1.25F);
    basis.forward = safe_normalize({std::sin(yaw) * std::cos(resolved_pitch),
                                    std::sin(resolved_pitch),
                                    -std::cos(yaw) * std::cos(resolved_pitch)},
                                   {0.0F, 0.0F, -1.0F});
    basis.right = safe_normalize(glm::cross(basis.forward, cubey::math::Vec3{0.0F, 1.0F, 0.0F}),
                                 {1.0F, 0.0F, 0.0F});
    basis.up = safe_normalize(glm::cross(basis.right, basis.forward), {0.0F, 1.0F, 0.0F});
    return basis;
}

[[nodiscard]] cubey::render::AtmosphereEnvironmentSolarPosition
clouds_solar_position(const CloudsConfig& config) {
    if (!config.time.solar_clock) {
        return {
            .elevation_degrees = config.time.manual_sun_elevation_degrees,
            .azimuth_degrees = config.time.manual_sun_azimuth_degrees,
        };
    }
    return cubey::render::atmosphere_environment_solar_position({
        .time_hours = config.time.time_hours,
        .day_of_year = config.time.day_of_year,
        .latitude_degrees = config.time.latitude_degrees,
        .azimuth_offset_degrees = config.time.azimuth_offset_degrees,
    });
}

[[nodiscard]] cubey::render::AtmosphereEnvironmentLighting
clouds_environment_lighting(const CloudsConfig& config,
                            const cubey::render::AtmosphereEnvironmentSolarPosition& solar) {
    cubey::render::AtmosphereEnvironmentConfig environment{};
    environment.time_of_day.time_hours = config.time.time_hours;
    environment.time_of_day.day_of_year = config.time.day_of_year;
    environment.time_of_day.latitude_degrees = config.time.latitude_degrees;
    environment.time_of_day.azimuth_offset_degrees = config.time.azimuth_offset_degrees;
    environment.sun_elevation_degrees = solar.elevation_degrees;
    environment.sun_azimuth_degrees = solar.azimuth_degrees;
    environment.camera_altitude_km = config.camera_altitude_m * 0.001F;
    environment.ground_mode =
        cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    environment.reference_geometry_enabled = false;
    return cubey::render::atmosphere_environment_lighting(environment);
}

[[nodiscard]] CloudsPushConstants clouds_push_constants(const CloudsConfig& config,
                                                        VkExtent2D extent, float yaw,
                                                        float pitch, float elapsed_seconds) {
    const CloudsViewBasis basis = clouds_view_basis(config, yaw, pitch);
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const float tan_half_fovy = std::tan(kDefaultFovyRadians * 0.5F);
    const CloudsQualityBudget budget = clouds_quality_budget(config.quality);
    const auto solar_position = clouds_solar_position(config);
    const cubey::math::Vec3 sun_direction =
        cubey::render::atmosphere_environment_direction_from_alt_az(
            solar_position.elevation_degrees, solar_position.azimuth_degrees);
    const cubey::render::AtmosphereEnvironmentLighting lighting =
        clouds_environment_lighting(config, solar_position);
    const float sun_intensity =
        std::min(lighting.sun_intensity * kCloudsSunIntensityScale, 1.15F);

    return {
        .camera_right_aspect = {basis.right.x, basis.right.y, basis.right.z, aspect},
        .camera_up_tan_half_fovy = {basis.up.x, basis.up.y, basis.up.z, tan_half_fovy},
        .camera_forward_mode = {basis.forward.x, basis.forward.y, basis.forward.z,
                                clouds_camera_shader_mode(config.camera_mode)},
        .camera_position_radius = {basis.position_km.x, basis.position_km.y, basis.position_km.z,
                                   config.planet_radius_m * 0.001F},
        .cloud_shell = {config.bottom_altitude_m * 0.001F, config.top_altitude_m * 0.001F,
                        lighting.moon_intensity, clouds_cloud_style_shader_value(config.cloud_style)},
        .weather = {config.coverage, config.density, config.weather_scale_km,
                    elapsed_seconds * config.wind_speed_mps * 0.001F},
        .sun_direction_intensity = {sun_direction.x, sun_direction.y, sun_direction.z,
                                    sun_intensity},
        .render_options = {static_cast<float>(static_cast<std::uint32_t>(config.debug_view)),
                           static_cast<float>(budget.view_steps),
                           static_cast<float>(budget.light_steps), config.shadow_strength},
    };
}

class CloudsApp {
  public:
    explicit CloudsApp(RunConfig run_config)
        : run_config_(std::move(run_config)),
          config_(clouds_config_from_run_config(run_config_)),
          home_config_(config_) {}

    CloudsApp(const CloudsApp&) = delete;
    CloudsApp& operator=(const CloudsApp&) = delete;

    ~CloudsApp() {
        destroy_swapchain_resources();
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
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_windowed(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) {
            draw_ui(context);
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context.device(), frame.command_buffer, frame.color_target,
                                  frame.frame_slot);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = run_config_,
                .app_name = "clouds",
                .ready_status = "rendering planet-aware clouds project",
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
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_pipeline(context.device(), target.format, target.extent,
                            cubey::host::headless_capture_frame_slot_count(run_config_));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            elapsed_seconds_ = static_cast<float>(frame.index) / static_cast<float>(run_config_.fps);
            CloudsConfig frame_config = config_;
            advance_clouds_time(frame_config, elapsed_seconds_);
            config_ = frame_config;
            record_headless_target(context.device(), command_buffer, target, frame.frame_slot);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_windowed(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::R)) {
            config_ = home_config_;
            yaw_ = 0.0F;
            pitch_ = 0.0F;
            elapsed_seconds_ = 0.0F;
            invalidate_cloud_history();
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            config_.debug_view = next_clouds_debug_view(config_.debug_view);
            invalidate_cloud_history();
        }
        if (input.key_pressed(cubey::input::Key::Space)) {
            config_.time.playing = !config_.time.playing;
        }
        if (input.mouse_enabled() && input.mouse_button_down(cubey::input::MouseButton::Left)) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Left);
            yaw_ -= static_cast<float>(delta.x) * kCameraDragRadiansPerPixel;
            pitch_ = clouds_clamp_camera_pitch_offset(
                config_.camera_mode,
                pitch_ - static_cast<float>(delta.y) * kCameraDragRadiansPerPixel);
            if (delta.x != 0.0 || delta.y != 0.0) {
                invalidate_cloud_history();
            }
        }
        elapsed_seconds_ += static_cast<float>(timing.delta_seconds);
        advance_clouds_time(config_, timing.delta_seconds);
        validate_clouds_config(config_);
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        if (!cubey::host::begin_control_panel("Clouds")) {
            ImGui::End();
            return;
        }

        if (cubey::host::imgui_button("Reset",
                                      "Restore the startup camera, time, and cloud settings.")) {
            config_ = home_config_;
            yaw_ = 0.0F;
            pitch_ = 0.0F;
            elapsed_seconds_ = 0.0F;
            ui_frame_stats_.reset();
            latest_frame_stats_.reset();
            invalidate_cloud_history();
        }

        if (cubey::host::imgui_enum_combo(
            "Debug view", config_.debug_view, kCloudsDebugViews, clouds_debug_view_name,
            "Inspect final color, cloud fields, background composition, shell hits, or "
            "raymarch step use.")) {
            invalidate_cloud_history();
        }
        if (cubey::host::imgui_enum_combo(
            "Quality", config_.quality, kCloudsQualityModes, clouds_quality_name,
            "Controls raymarch view and light sample budgets.")) {
            invalidate_cloud_history();
        }
        if (cubey::host::imgui_checkbox(
                "Temporal", &config_.temporal_enabled,
                "Accumulate the cloud product across frames. Disable this to inspect raw "
                "raymarch output and temporal reconstruction artifacts.")) {
            invalidate_cloud_history();
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Camera", {.help = "Camera preset and orbit/surface framing controls."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Camera");
            CloudsCameraMode selected_mode = config_.camera_mode;
            if (cubey::host::imgui_enum_combo("Mode", selected_mode, kCloudsCameraModes,
                                              clouds_camera_mode_name,
                                              "Surface, upward surface, high top-down, high "
                                              "oblique, orbit, and orbit terminator inspection "
                                              "presets.")) {
                set_camera_mode(selected_mode);
            }
            ImGui::InputFloat("Altitude", &config_.camera_altitude_m, 0.0F, 0.0F, "%.0f");
            cubey::host::imgui_attach_help("Camera altitude above the planet surface in meters.");
            ImGui::Text("Yaw / pitch: %.2f / %.2f rad", yaw_, pitch_);
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Weather", {.help = "Coverage, density, wind, and macro weather scale."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Weather");
            CloudsWeatherPreset selected_preset = config_.weather_preset;
            if (cubey::host::imgui_enum_combo("Preset", selected_preset, kCloudsWeatherPresets,
                                              clouds_weather_preset_name,
                                              "Cloud type and weather preset.")) {
                apply_clouds_weather_preset(config_, selected_preset);
                invalidate_cloud_history();
            }
            if (cubey::host::imgui_slider_float(
                    "Coverage", &config_.coverage, 0.0F, 1.0F, "%.2f",
                    "Amount of sky covered by the procedural weather map.")) {
                invalidate_cloud_history();
            }
            if (cubey::host::imgui_slider_float(
                    "Density", &config_.density, 0.0F, 2.0F, "%.2f",
                    "Optical density multiplier for the cloud volume.")) {
                invalidate_cloud_history();
            }
            if (cubey::host::imgui_slider_float(
                    "Scale", &config_.weather_scale_km, 20.0F, 500.0F, "%.0f km",
                    "Horizontal scale of the broad weather field.")) {
                invalidate_cloud_history();
            }
            cubey::host::imgui_slider_float("Wind", &config_.wind_speed_mps, 0.0F, 80.0F,
                                            "%.1f m/s",
                                            "Advection speed for procedural cloud features.");
            cubey::host::imgui_slider_float(
                "Shadow", &config_.shadow_strength, 0.0F, 2.0F, "%.2f",
                "Strength of prototype cloud shadows on the standalone ground proxy.");
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Layer", {.default_open = false,
                           .help = "Cloud-shell altitude range above the planet surface."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Layer");
            if (ImGui::InputFloat("Bottom", &config_.bottom_altitude_m, 0.0F, 0.0F, "%.0f")) {
                invalidate_cloud_history();
            }
            cubey::host::imgui_attach_help("Cloud layer bottom altitude in meters.");
            if (ImGui::InputFloat("Top", &config_.top_altitude_m, 0.0F, 0.0F, "%.0f")) {
                invalidate_cloud_history();
            }
            cubey::host::imgui_attach_help("Cloud layer top altitude in meters.");
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Time", {.default_open = false,
                          .help = "Solar-clock controls used for cloud lighting."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Time");
            cubey::host::imgui_checkbox("Solar clock", &config_.time.solar_clock,
                                        "Compute sun direction from time, day, latitude, and "
                                        "azimuth offset.");
            cubey::host::imgui_checkbox("Play", &config_.time.playing,
                                        "Advance the solar clock every frame.");
            cubey::host::imgui_slider_float("Time", &config_.time.time_hours, 0.0F, 24.0F,
                                            "%.2f h", "Local solar time in hours.");
            cubey::host::imgui_slider_float("Day", &config_.time.day_of_year, 1.0F, 366.0F,
                                            "%.0f", "Day of year for solar position.");
            cubey::host::imgui_slider_float("Latitude", &config_.time.latitude_degrees, -90.0F,
                                            90.0F, "%.1f deg",
                                            "Observer latitude used by the solar clock.");
            cubey::host::imgui_slider_float("Azimuth offset",
                                            &config_.time.azimuth_offset_degrees, -180.0F, 180.0F,
                                            "%.1f deg",
                                            "Horizontal rotation applied to the solar clock.");
            cubey::host::imgui_slider_float("Speed", &config_.time.speed_hours_per_second, 0.0F,
                                            12.0F, "%.2f h/s",
                                            "Simulated hours advanced per real second.");
        }

        const auto solar = clouds_solar_position(config_);
        const cubey::render::AtmosphereEnvironmentLighting lighting =
            clouds_environment_lighting(config_, solar);
        if (const cubey::host::ScopedImGuiGroup group{
                "Diagnostics", {.default_open = false,
                                 .help = "Read-only cloud camera, shell, and solar state."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Diagnostics");
            ImGui::Text("Camera: %s / %.0f m", clouds_camera_mode_name(config_.camera_mode),
                        config_.camera_altitude_m);
            ImGui::Text("Layer: %.0f m - %.0f m", config_.bottom_altitude_m,
                        config_.top_altitude_m);
            ImGui::Text("Weather phase: %.2f km",
                        elapsed_seconds_ * config_.wind_speed_mps * 0.001F);
            ImGui::Text("Sun: %.1f elev / %.1f az", solar.elevation_degrees,
                        solar.azimuth_degrees);
            ImGui::Text("Cloud light: %.2f sun / %.3f moon",
                        std::min(lighting.sun_intensity * kCloudsSunIntensityScale, 1.15F),
                        lighting.moon_intensity);
            ImGui::TextUnformatted("Keys: D debug, Space solar play, R reset");
        }

        const CloudsQualityBudget budget = clouds_quality_budget(config_.quality);
        const VkExtent2D output_extent = context.swapchain().extent();
        const VkExtent2D cloud_extent = clouds_scaled_extent(output_extent, budget,
                                                             config_.camera_mode);
        const std::array<cubey::host::PerformanceCounter, 8> performance_counters{
            cubey::host::PerformanceCounter{"View steps",
                                            static_cast<std::uint64_t>(budget.view_steps), nullptr},
            cubey::host::PerformanceCounter{"Light steps",
                                            static_cast<std::uint64_t>(budget.light_steps),
                                            nullptr},
            cubey::host::PerformanceCounter{
                "Max samples / px",
                static_cast<std::uint64_t>(budget.view_steps * (1 + budget.light_steps)),
                nullptr},
            cubey::host::PerformanceCounter{
                "Cloud scale X",
                static_cast<std::uint64_t>(clouds_resolution_scale_x(budget) * 100.0F), "%"},
            cubey::host::PerformanceCounter{
                "Cloud scale Y",
                static_cast<std::uint64_t>(
                    clouds_resolution_scale_y(budget, config_.camera_mode) * 100.0F),
                "%"},
            cubey::host::PerformanceCounter{"Cloud pixels", extent_pixel_count(cloud_extent),
                                            nullptr},
            cubey::host::PerformanceCounter{"Output pixels", extent_pixel_count(output_extent),
                                            nullptr},
            cubey::host::PerformanceCounter{"Fullscreen tris", 2, nullptr},
        };
        cubey::host::draw_performance_ui({
            .frame_stats = latest_frame_stats_,
            .latest_fps = latest_fps_,
            .latest_frame_ms = latest_frame_ms_,
            .process = process_stats_.sample(),
            .device_memory_budget = context.device().device_memory_budget(),
            .counters = performance_counters,
            .config = {.default_open = true},
        });
        ImGui::End();
    }

    [[nodiscard]] std::optional<cubey::host::FrameStatsSample>
    record_frame_stats(VkExtent2D extent, const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const cubey::host::FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = 2,
        };
        if (std::optional<cubey::host::FrameStatsSnapshot> stats =
                ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void set_camera_mode(CloudsCameraMode mode) {
        if (config_.camera_mode == mode) {
            return;
        }
        config_.camera_mode = mode;
        config_.camera_altitude_m = clouds_default_camera_altitude_m(mode);
        if (mode == CloudsCameraMode::OrbitTerminator) {
            config_.time.time_hours = 6.0F;
        }
        yaw_ = 0.0F;
        pitch_ = clouds_clamp_camera_pitch_offset(mode, 0.0F);
        invalidate_cloud_history();
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        composite_sampler_.reset();
        composite_material_.reset();
        composite_pipeline_resource_.reset();
        temporal_material_.reset();
        temporal_pipeline_resource_.reset();
        cloud_pipeline_resource_.reset();
        cloud_history_textures_.clear();
        cloud_history_read_indices_.clear();
        cloud_history_texture_valid_.clear();
    }

    void invalidate_cloud_history() {
        for (std::array<bool, 2>& valid : cloud_history_texture_valid_) {
            valid = {false, false};
        }
        std::fill(cloud_history_read_indices_.begin(), cloud_history_read_indices_.end(), 0U);
    }

    void create_cloud_history_textures(const cubey::vulkan::Device& device, VkExtent2D extent,
                                       std::uint32_t frame_slot_count) {
        cloud_history_textures_.clear();
        cloud_history_read_indices_.assign(frame_slot_count, 0U);
        cloud_history_texture_valid_.assign(frame_slot_count, {false, false});
        cloud_history_textures_.resize(frame_slot_count);
        for (std::array<std::optional<cubey::render::Texture2D>, 2>& slot_textures :
             cloud_history_textures_) {
            for (std::optional<cubey::render::Texture2D>& texture : slot_textures) {
                texture.emplace(device, cubey::render::Texture2DConfig{
                                            .extent = extent,
                                            .format = kCloudsSceneColorFormat,
                                            .usage =
                                                cubey::render::Texture2DUsage::
                                                    ColorAttachmentSampled,
                                            .create_sampler = false,
                                            .sampler = {},
                                        });
            }
        }
    }

    [[nodiscard]] const cubey::render::Texture2D&
    cloud_history_texture(cubey::render::FrameSlot frame_slot, std::uint32_t ping_pong) const {
        if (frame_slot.index >= cloud_history_textures_.size() || ping_pong >= 2U ||
            !cloud_history_textures_[frame_slot.index][ping_pong].has_value()) {
            throw std::runtime_error("cloud history texture is not initialized");
        }
        return cloud_history_textures_[frame_slot.index][ping_pong].value();
    }

    [[nodiscard]] bool
    cloud_history_texture_valid(cubey::render::FrameSlot frame_slot,
                                std::uint32_t ping_pong) const noexcept {
        return frame_slot.index < cloud_history_texture_valid_.size() && ping_pong < 2U &&
               cloud_history_texture_valid_[frame_slot.index][ping_pong];
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count) {
        graph_executor_.resize(frame_slot_count);
        const VkExtent2D cloud_extent =
            clouds_scaled_extent(extent, clouds_quality_budget(config_.quality),
                                 config_.camera_mode);

        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("clouds.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("clouds.frag.spv")),
        };
        const cubey::render::MaterialPassInfo material_pass = clouds_pass_info();
        cloud_pipeline_resource_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                                     .extent = cloud_extent,
                                                     .color_formats =
                                                         {
                                                             kCloudsSceneColorFormat,
                                                             kCloudsMetadataFormat,
                                                         },
                                                     .shader_stage_files = shader_stage_files,
                                                     .material_pass = material_pass,
                                                 });
        create_cloud_history_textures(device, cloud_extent, frame_slot_count);

        const cubey::render::MaterialPassInfo temporal_pass = clouds_temporal_pass_info();
        temporal_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                               .material_pass = temporal_pass,
                                               .descriptor_set = 0,
                                               .set_count = frame_slot_count,
                                           });
        const std::array<cubey::render::ShaderStageFile, 2> temporal_shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("clouds.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("clouds_temporal.frag.spv")),
        };
        const std::array<VkDescriptorSetLayout, 1> temporal_set_layouts{
            temporal_material_->layout(),
        };
        temporal_pipeline_resource_.emplace(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = cloud_extent,
                        .color_format = kCloudsSceneColorFormat,
                        .shader_stage_files = temporal_shader_stage_files,
                        .descriptor_set_layouts = temporal_set_layouts,
                        .material_pass = temporal_pass,
                    });

        const cubey::render::MaterialPassInfo composite_pass = clouds_composite_pass_info();
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
        const std::array<cubey::render::ShaderStageFile, 2> composite_shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("clouds.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("clouds_composite.frag.spv")),
        };
        const std::array<VkDescriptorSetLayout, 1> composite_set_layouts{
            composite_material_->layout(),
        };
        composite_pipeline_resource_.emplace(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = extent,
                        .color_format = color_format,
                        .shader_stage_files = composite_shader_stage_files,
                        .descriptor_set_layouts = composite_set_layouts,
                        .material_pass = composite_pass,
                    });
    }

    void record_clouds_draw(const cubey::vulkan::CommandRecorder& recorder,
                            const cubey::render::ColorTargetView& target,
                            const cubey::render::ColorTargetView& metadata_target,
                            VkExtent2D view_extent) const {
        CloudsPushConstants constants =
            clouds_push_constants(config_, view_extent, yaw_, pitch_, elapsed_seconds_);
        constants.render_options.w = static_cast<float>(temporal_frame_index_ % 256U);
        const std::array<cubey::render::ColorTargetView, 2> targets{
            target,
            metadata_target,
        };
        cubey::render::record_render_target_pass(
            recorder, cubey::render::render_target_view(targets),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            },
            [this, constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder, {.pipeline = &cloud_pipeline_resource()},
                    VK_SHADER_STAGE_FRAGMENT_BIT, constants);
            });
    }

    [[nodiscard]] CloudsTemporalPushConstants
    clouds_temporal_push_constants(cubey::render::FrameSlot frame_slot) const {
        const std::uint32_t history_read_index =
            frame_slot.index < cloud_history_read_indices_.size()
                ? cloud_history_read_indices_[frame_slot.index]
                : 0U;
        const bool reset = !cloud_history_texture_valid(frame_slot, history_read_index);
        const float current_weight = reset ? 1.0F : 0.22F;
        return {
            .options =
                {
                    current_weight,
                    reset ? 1.0F : 0.0F,
                    static_cast<float>(temporal_frame_index_ % 256U),
                    0.0F,
                },
        };
    }

    void record_temporal_draw(const cubey::vulkan::CommandRecorder& recorder,
                              const cubey::render::ColorTargetView& target,
                              cubey::render::FrameSlot frame_slot) const {
        const CloudsTemporalPushConstants constants = clouds_temporal_push_constants(frame_slot);
        cubey::render::record_render_target_pass(
            recorder, cubey::render::render_target_view(target),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            },
            [this, frame_slot, constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder,
                    {.pipeline = &temporal_pipeline_resource(),
                     .descriptor_set = temporal_material().set(frame_slot)},
                    VK_SHADER_STAGE_FRAGMENT_BIT, constants);
            });
    }

    void record_composite_draw(const cubey::vulkan::CommandRecorder& recorder,
                               const cubey::render::ColorTargetView& target,
                               cubey::render::FrameSlot frame_slot) const {
        const CloudsPushConstants constants =
            clouds_push_constants(config_, target.extent, yaw_, pitch_, elapsed_seconds_);
        cubey::render::record_render_target_pass(
            recorder, cubey::render::render_target_view(target),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            },
            [this, frame_slot, constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder,
                    {.pipeline = &composite_pipeline_resource(),
                     .descriptor_set = composite_material().set(frame_slot)},
                    VK_SHADER_STAGE_FRAGMENT_BIT, constants);
            });
    }

    [[nodiscard]] CloudsFrameGraph build_frame_graph(
        cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
        CloudsRenderTargetMode target_mode) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer =
            graph.import_color_target("backbuffer", target, clouds_target_initial_state(target_mode),
                                      clouds_target_final_state(target_mode));
        const VkExtent2D cloud_extent =
            clouds_scaled_extent(target.extent, clouds_quality_budget(config_.quality),
                                 config_.camera_mode);
        const std::uint32_t history_read_index =
            frame_slot.index < cloud_history_read_indices_.size()
                ? cloud_history_read_indices_[frame_slot.index]
                : 0U;
        const std::uint32_t history_write_index = 1U - history_read_index;
        const cubey::render::RenderGraphTextureState sampled_state =
            clouds_sampled_color_texture_state();
        const bool history_valid = cloud_history_texture_valid(frame_slot, history_read_index);
        const bool history_write_valid =
            cloud_history_texture_valid(frame_slot, history_write_index);
        cubey::render::RenderGraphTextureHandle cloud_history_read{};
        const bool temporal_pass_enabled = config_.temporal_enabled && history_valid;
        if (temporal_pass_enabled) {
            const cubey::render::Texture2D& read_texture =
                cloud_history_texture(frame_slot, history_read_index);
            cloud_history_read = graph.import_texture(
                clouds_color_texture_desc("cloud history read", cloud_extent,
                                          kCloudsSceneColorFormat),
                read_texture.handle(), read_texture.view(), sampled_state, sampled_state);
        }
        const cubey::render::Texture2D& write_texture =
            cloud_history_texture(frame_slot, history_write_index);
        const cubey::render::RenderGraphTextureHandle cloud_history_write = graph.import_texture(
            clouds_color_texture_desc("cloud history write", cloud_extent, kCloudsSceneColorFormat),
            write_texture.handle(), write_texture.view(),
            history_write_valid
                ? std::optional<cubey::render::RenderGraphTextureState>{sampled_state}
                : std::optional<cubey::render::RenderGraphTextureState>{
                      cubey::render::render_graph_undefined_texture_state()},
            sampled_state);

        cubey::render::RenderGraphTextureHandle cloud_color{};
        cubey::render::RenderGraphTextureHandle cloud_metadata =
            graph.create_texture(clouds_color_texture_desc("cloud metadata", cloud_extent,
                                                           kCloudsMetadataFormat));
        if (temporal_pass_enabled) {
            cloud_color =
                graph.create_texture(clouds_color_texture_desc("cloud product", cloud_extent,
                                                               kCloudsSceneColorFormat));
            graph.add_pass("cloud raymarch", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_color(cloud_color)
                .write_color(cloud_metadata)
                .execute(
                    [this, cloud_color, cloud_metadata, view_extent = target.extent](
                        const cubey::render::RenderGraphExecutionContext& context) {
                        record_clouds_draw(
                            context.recorder(),
                            cubey::render::resolved_color_target_view(context, cloud_color),
                            cubey::render::resolved_color_target_view(context, cloud_metadata),
                            view_extent);
                    });
            graph.add_pass("cloud temporal", cubey::render::RenderGraphQueueDomain::Graphics)
                .read_texture(cloud_color)
                .read_texture(cloud_history_read)
                .write_color(cloud_history_write)
                .execute([this, cloud_history_write,
                          frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                    record_temporal_draw(
                        context.recorder(),
                        cubey::render::resolved_color_target_view(context, cloud_history_write),
                        frame_slot);
                });
        } else {
            graph.add_pass("cloud raymarch", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_color(cloud_history_write)
                .write_color(cloud_metadata)
                .execute([this, cloud_history_write, cloud_metadata, view_extent = target.extent](
                             const cubey::render::RenderGraphExecutionContext& context) {
                    record_clouds_draw(
                        context.recorder(),
                        cubey::render::resolved_color_target_view(context, cloud_history_write),
                        cubey::render::resolved_color_target_view(context, cloud_metadata),
                        view_extent);
                });
        }
        graph.add_pass("cloud composite", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(cloud_history_write)
            .read_texture(cloud_metadata)
            .write_color(backbuffer)
            .execute([this, backbuffer,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_composite_draw(context.recorder(),
                                      cubey::render::resolved_color_target_view(context, backbuffer),
                                      frame_slot);
            });

        return {
            .graph = graph.compile(),
            .cloud_color = cloud_color,
            .cloud_metadata = cloud_metadata,
            .cloud_history_read = cloud_history_read,
            .cloud_history_write = cloud_history_write,
            .resolved_cloud_color = cloud_history_write,
            .cloud_extent = cloud_extent,
            .temporal_pass_enabled = temporal_pass_enabled,
            .history_write_index = history_write_index,
        };
    }

    void record_frame_graph(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                            const cubey::render::ColorTargetView& target,
                            cubey::render::FrameSlot frame_slot, CloudsRenderTargetMode target_mode,
                            cubey::render::RenderGraphCommandBufferMode command_buffer_mode,
                            const char* label) {
        const CloudsFrameGraph frame_graph = build_frame_graph(target, frame_slot, target_mode);
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
                if (frame_graph.temporal_pass_enabled) {
                    const cubey::render::RenderGraphSampledTextureView cloud_color =
                        cubey::render::resolved_sampled_texture_view(
                            frame_graph.graph, graph_resources, frame_graph.cloud_color);
                    const cubey::render::RenderGraphSampledTextureView history_color =
                        cubey::render::resolved_sampled_texture_view(
                            frame_graph.graph, graph_resources,
                            frame_graph.cloud_history_read);
                    cubey::render::MaterialDescriptorWriter(temporal_material().set(frame_slot))
                        .combined_image_sampler(0, composite_sampler().handle(), cloud_color.view,
                                                cloud_color.layout)
                        .combined_image_sampler(1, composite_sampler().handle(), history_color.view,
                                                history_color.layout)
                        .update(device);
                }
                const cubey::render::RenderGraphSampledTextureView resolved_cloud_color =
                    cubey::render::resolved_sampled_texture_view(
                        frame_graph.graph, graph_resources, frame_graph.resolved_cloud_color);
                const cubey::render::RenderGraphSampledTextureView resolved_cloud_metadata =
                    cubey::render::resolved_sampled_texture_view(
                        frame_graph.graph, graph_resources, frame_graph.cloud_metadata);
                cubey::render::MaterialDescriptorWriter(composite_material().set(frame_slot))
                    .combined_image_sampler(0, composite_sampler().handle(),
                                            resolved_cloud_color.view,
                                            resolved_cloud_color.layout)
                    .combined_image_sampler(1, composite_sampler().handle(),
                                            resolved_cloud_metadata.view,
                                            resolved_cloud_metadata.layout)
                    .update(device);
            });
        if (frame_slot.index < cloud_history_texture_valid_.size() &&
            frame_slot.index < cloud_history_read_indices_.size()) {
            cloud_history_texture_valid_[frame_slot.index][frame_graph.history_write_index] = true;
            cloud_history_read_indices_[frame_slot.index] = frame_graph.history_write_index;
        }
        ++temporal_frame_index_;
    }

    void record_windowed_frame(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                               const cubey::render::ColorTargetView& target,
                               cubey::render::FrameSlot frame_slot) {
        record_frame_graph(device, command_buffer, target, frame_slot, CloudsRenderTargetMode::Present,
                           cubey::render::RenderGraphCommandBufferMode::BeginAndEnd,
                           "vkEndCommandBuffer clouds");
    }

    void record_headless_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                                const cubey::render::ColorTargetView& target,
                                cubey::render::FrameSlot frame_slot) {
        record_frame_graph(device, command_buffer, target, frame_slot,
                           CloudsRenderTargetMode::ColorAttachment,
                           cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                           "vkEndCommandBuffer clouds headless");
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& cloud_pipeline_resource() const {
        if (!cloud_pipeline_resource_.has_value()) {
            throw std::runtime_error("cloud pipeline resource is not initialized");
        }
        return cloud_pipeline_resource_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& composite_pipeline_resource() const {
        if (!composite_pipeline_resource_.has_value()) {
            throw std::runtime_error("cloud composite pipeline resource is not initialized");
        }
        return composite_pipeline_resource_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& temporal_pipeline_resource() const {
        if (!temporal_pipeline_resource_.has_value()) {
            throw std::runtime_error("cloud temporal pipeline resource is not initialized");
        }
        return temporal_pipeline_resource_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& composite_material() const {
        if (!composite_material_.has_value()) {
            throw std::runtime_error("cloud composite material is not initialized");
        }
        return composite_material_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& temporal_material() const {
        if (!temporal_material_.has_value()) {
            throw std::runtime_error("cloud temporal material is not initialized");
        }
        return temporal_material_.value();
    }

    [[nodiscard]] const cubey::vulkan::Sampler& composite_sampler() const {
        if (!composite_sampler_.has_value()) {
            throw std::runtime_error("cloud composite sampler is not initialized");
        }
        return composite_sampler_.value();
    }

    RunConfig run_config_;
    CloudsConfig config_;
    CloudsConfig home_config_;
    float elapsed_seconds_ = 0.0F;
    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<cubey::host::FrameStatsSnapshot> latest_frame_stats_;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    std::optional<cubey::render::GraphicsPipelineResource> cloud_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> temporal_pipeline_resource_;
    std::optional<cubey::render::GraphicsPipelineResource> composite_pipeline_resource_;
    std::optional<cubey::render::MaterialInstance> temporal_material_;
    std::optional<cubey::render::MaterialInstance> composite_material_;
    std::optional<cubey::vulkan::Sampler> composite_sampler_;
    std::vector<std::array<std::optional<cubey::render::Texture2D>, 2>> cloud_history_textures_;
    std::vector<std::uint32_t> cloud_history_read_indices_;
    std::vector<std::array<bool, 2>> cloud_history_texture_valid_;
    std::uint32_t temporal_frame_index_ = 0;
};

} // namespace

int run_clouds(const RunConfig& config) {
    CloudsApp app(config);
    return app.run();
}

} // namespace cubey::projects::clouds
