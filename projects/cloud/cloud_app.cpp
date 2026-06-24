#include "cloud_app.h"

#include "cloud_config.h"

#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/cloud_layer.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
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
constexpr float kOrbitMaxLatitudeRadians = 1.30F;

constexpr std::array<CloudsCameraMode, 6> kCloudCameraModes{
    CloudsCameraMode::Surface, CloudsCameraMode::SurfaceUp, CloudsCameraMode::High,
    CloudsCameraMode::HighOblique, CloudsCameraMode::Orbit, CloudsCameraMode::OrbitTerminator,
};
constexpr std::array<CloudsQuality, 3> kCloudQualityModes{
    CloudsQuality::Quarter,
    CloudsQuality::Half,
    CloudsQuality::Full,
};
constexpr std::array<CloudsWeatherPreset, 6> kCloudWeatherPresets{
    CloudsWeatherPreset::Clear,
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
constexpr std::array<CloudsBackgroundMode, 2> kCloudBackgroundModes{
    CloudsBackgroundMode::Atmosphere,
    CloudsBackgroundMode::WaterContext,
};
constexpr std::array<CloudsDistanceMode, 4> kCloudDistanceModes{
    CloudsDistanceMode::Auto,
    CloudsDistanceMode::Local,
    CloudsDistanceMode::OrbitShell,
    CloudsDistanceMode::BlendDebug,
};
constexpr std::array<CloudsOrbitRepresentation, 2> kCloudOrbitRepresentations{
    CloudsOrbitRepresentation::VolumeRaymarch,
    CloudsOrbitRepresentation::SurfaceShell,
};
constexpr std::array<CloudsDebugView, 43> kCloudDebugViews{
    CloudsDebugView::Final,        CloudsDebugView::RawFinal,
    CloudsDebugView::AuthoredWeather,
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
    CloudsDebugView::LocalScatter,
    CloudsDebugView::LocalClear,
    CloudsDebugView::LocalStructure,
    CloudsDebugView::LocalEdgeDetail,
    CloudsDebugView::WeatherEdge,   CloudsDebugView::CoverageBias,
    CloudsDebugView::VisibleDensity,
    CloudsDebugView::VisibleCloudType,
    CloudsDebugView::DistanceRegime,
    CloudsDebugView::TransitionWeights,
    CloudsDebugView::LocalAlpha,
    CloudsDebugView::FarShellAlpha,
    CloudsDebugView::LocalWithShellAlpha,
    CloudsDebugView::OrbitAlpha,
    CloudsDebugView::OrbitCoverage,
    CloudsDebugView::OrbitDetail,
    CloudsDebugView::OrbitHull,
    CloudsDebugView::OrbitEnvelope,
    CloudsDebugView::OrbitShellAlpha,
    CloudsDebugView::OrbitShellHeight,
    CloudsDebugView::OrbitShellNormal,
    CloudsDebugView::OrbitShellShadow,
};

using CloudFrameUniforms = cubey::render::CloudLayerFrameUniforms;

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
    cubey::render::CloudLayerRuntimeFrame cloud{};
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_CLOUD_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files() {
    return {
        .generated =
            {
                .base_noise =
                    cubey::render::compute_shader_file(shader_path("cloud_perlin_worley.comp.spv")),
                .detail_noise =
                    cubey::render::compute_shader_file(shader_path("cloud_worley.comp.spv")),
                .weather = cubey::render::compute_shader_file(shader_path("cloud_weather.comp.spv")),
            },
        .march = cubey::render::compute_shader_file(shader_path("cloud_march.comp.spv")),
        .temporal = cubey::render::compute_shader_file(shader_path("cloud_temporal.comp.spv")),
        .composite_vertex = cubey::render::vertex_shader_file(shader_path("cloud.vert.spv")),
        .composite_fragment =
            cubey::render::fragment_shader_file(shader_path("cloud_composite.frag.spv")),
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

[[nodiscard]] bool cloud_camera_mode_is_orbit(CloudsCameraMode mode) {
    return mode == CloudsCameraMode::Orbit || mode == CloudsCameraMode::OrbitTerminator;
}

[[nodiscard]] cubey::math::Vec3 safe_normalize(cubey::math::Vec3 value,
                                               cubey::math::Vec3 fallback) {
    const float len2 = glm::dot(value, value);
    if (len2 <= 0.0000001F) {
        return fallback;
    }
    return value * glm::inversesqrt(len2);
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
        return 0.50F;
    case CloudsCameraMode::Surface:
    default:
        return -0.06F;
    }
}

[[nodiscard]] float cloud_min_pitch(CloudsCameraMode mode) {
    switch (mode) {
    case CloudsCameraMode::Orbit:
    case CloudsCameraMode::OrbitTerminator:
        return -kOrbitMaxLatitudeRadians;
    case CloudsCameraMode::Surface:
    case CloudsCameraMode::SurfaceUp:
    case CloudsCameraMode::High:
    case CloudsCameraMode::HighOblique:
    default:
        return kSurfaceMinPitchRadians;
    }
}

[[nodiscard]] float cloud_clamp_pitch(CloudsCameraMode mode, float pitch_offset) {
    const float base_pitch = cloud_camera_base_pitch(mode);
    const float max_pitch =
        cloud_camera_mode_is_orbit(mode) ? kOrbitMaxLatitudeRadians : kSurfaceMaxPitchRadians;
    return std::clamp(base_pitch + pitch_offset, cloud_min_pitch(mode), max_pitch) - base_pitch;
}

[[nodiscard]] float cloud_camera_shader_mode(CloudsCameraMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] CloudViewBasis cloud_view_basis(const CloudsConfig& config, float yaw,
                                                     float pitch_offset) {
    const cubey::math::Vec3 surface_up{0.0F, 1.0F, 0.0F};
    if (cloud_camera_mode_is_orbit(config.camera_mode)) {
        const float orbit_pitch =
            std::clamp(cloud_camera_base_pitch(config.camera_mode) + pitch_offset,
                       -kOrbitMaxLatitudeRadians, kOrbitMaxLatitudeRadians);
        const float camera_radius = config.planet_radius_m + config.camera_altitude_m;
        const cubey::math::Vec3 planet_center{0.0F, -config.planet_radius_m, 0.0F};
        const cubey::math::Vec3 radial{
            std::sin(yaw) * std::cos(orbit_pitch),
            std::sin(orbit_pitch),
            std::cos(yaw) * std::cos(orbit_pitch),
        };
        const cubey::math::Vec3 position = planet_center + radial * camera_radius;
        const cubey::math::Vec3 forward =
            safe_normalize(planet_center - position, {0.0F, -1.0F, 0.0F});
        const cubey::math::Vec3 right =
            safe_normalize(glm::cross(forward, surface_up), {1.0F, 0.0F, 0.0F});
        return {
            .position = position,
            .right = right,
            .up = safe_normalize(glm::cross(right, forward), {0.0F, 1.0F, 0.0F}),
            .forward = forward,
        };
    }

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

[[nodiscard]] cubey::render::CloudLayerConfig cloud_layer_config(const CloudsConfig& config,
                                                                 float elapsed_seconds) {
    return {
        .quality =
            static_cast<cubey::render::CloudLayerQuality>(static_cast<std::uint32_t>(
                config.quality)),
        .cloud_style =
            static_cast<cubey::render::CloudLayerCloudStyle>(static_cast<std::uint32_t>(
                config.cloud_style)),
        .sampling_mode =
            static_cast<cubey::render::CloudLayerSamplingMode>(static_cast<std::uint32_t>(
                config.sampling_mode)),
        .background_mode =
            static_cast<cubey::render::CloudLayerBackgroundMode>(static_cast<std::uint32_t>(
                config.background_mode)),
        .distance_mode =
            static_cast<cubey::render::CloudLayerDistanceMode>(static_cast<std::uint32_t>(
                config.distance_mode)),
        .orbit_representation =
            static_cast<cubey::render::CloudLayerOrbitRepresentation>(
                static_cast<std::uint32_t>(config.orbit_representation)),
        .debug_view =
            static_cast<cubey::render::CloudLayerDebugView>(static_cast<std::uint32_t>(
                config.debug_view)),
        .temporal_enabled = config.temporal_enabled,
        .powder_enabled = config.powder_enabled,
        .local_volume_enabled = config.local_volume_enabled,
        .horizon_layer_enabled = config.horizon_layer_enabled,
        .planet_radius_m = config.planet_radius_m,
        .bottom_altitude_m = config.bottom_altitude_m,
        .top_altitude_m = config.top_altitude_m,
        .coverage = config.coverage,
        .density = config.density,
        .weather_scale_km = config.weather_scale_km,
        .vertical_shear_fraction = config.vertical_shear_fraction,
        .wind_offset_m = elapsed_seconds * config.wind_speed_mps,
        .shadow_strength = config.shadow_strength,
        .horizon_strength = config.horizon_strength,
        .weather_fronts = config.weather_fronts,
        .weather_cells = config.weather_cells,
        .weather_streaks = config.weather_streaks,
        .weather_softness = config.weather_softness,
        .weather_influence = config.weather_influence,
        .detail_erosion = config.detail_erosion,
        .crispiness = config.crispiness,
        .curliness = config.curliness,
        .absorption = config.absorption,
        .ambient_strength = config.ambient_strength,
        .direct_strength = config.direct_strength,
        .phase_strength = config.phase_strength,
        .final_contrast = config.final_contrast,
        .final_saturation = config.final_saturation,
        .resolve_strength = config.resolve_strength,
        .horizon_glow_strength = config.horizon_glow_strength,
        .sun_glare_strength = config.sun_glare_strength,
        .jitter_strength = config.jitter_strength,
        .orbit_transition_start_m = config.orbit_transition_start_m,
        .orbit_transition_end_m = config.orbit_transition_end_m,
        .far_shell_start_m = config.far_shell_start_m,
        .far_shell_end_m = config.far_shell_end_m,
        .far_shell_strength = config.far_shell_strength,
        .orbit_detail_strength = config.orbit_detail_strength,
        .orbit_density_scale = config.orbit_density_scale,
        .orbit_fill = config.orbit_fill,
        .orbit_motion_strength = config.orbit_motion_strength,
        .orbit_shell_extinction = config.orbit_shell_extinction,
    };
}

[[nodiscard]] CloudFrameUniforms cloud_frame_uniforms(const CloudsConfig& config,
                                                            VkExtent2D target_extent,
                                                            float yaw, float pitch,
                                                            float elapsed_seconds,
                                                            std::uint32_t temporal_frame_index) {
    const CloudViewBasis basis = cloud_view_basis(config, yaw, pitch);
    const float tan_half_fovy = std::tan(kDefaultFovyRadians * 0.5F);
    return cubey::render::cloud_layer_frame_uniforms(
        cloud_layer_config(config, elapsed_seconds),
        cubey::render::CloudLayerFrameInfo{
            .camera_position = basis.position,
            .camera_right = basis.right,
            .camera_up = basis.up,
            .camera_forward = basis.forward,
            .tan_half_fovy = tan_half_fovy,
            .sun_direction = cloud_source_sun_direction(),
            .sun_intensity = 1.0F,
            .target_extent = target_extent,
            .temporal_frame_index = temporal_frame_index,
            .camera_mode = cloud_camera_shader_mode(config.camera_mode),
        });
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
            refresh_weather_texture_if_needed(context.device(), context.gpu());
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
            refresh_weather_texture_if_needed(context.device(), context.gpu());
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
            const bool orbit_camera = cloud_camera_mode_is_orbit(config_.camera_mode);
            const float yaw_delta = static_cast<float>(delta.x) * kCameraDragRadiansPerPixel;
            const float pitch_delta = static_cast<float>(delta.y) * kCameraDragRadiansPerPixel;
            yaw_ += orbit_camera ? -yaw_delta : yaw_delta;
            pitch_ = cloud_clamp_pitch(config_.camera_mode,
                                       pitch_ + (orbit_camera ? pitch_delta : -pitch_delta));
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
        draw_enum_combo("Debug view", config_.debug_view, kCloudDebugViews,
                        clouds_debug_view_name);
        draw_enum_combo("Background", config_.background_mode, kCloudBackgroundModes,
                        clouds_background_mode_name);
        draw_enum_combo("Distance mode", config_.distance_mode, kCloudDistanceModes,
                        clouds_distance_mode_name);
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
        if (ImGui::CollapsingHeader("Distance / Transition",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Local volume", &config_.local_volume_enabled);
            ImGui::Checkbox("Surface horizon assist", &config_.horizon_layer_enabled);
            ImGui::SliderFloat("Orbit takeover start", &config_.orbit_transition_start_m, 0.0F,
                               120000.0F, "%.0f m");
            ImGui::SliderFloat("Orbit takeover end", &config_.orbit_transition_end_m, 1000.0F,
                               240000.0F, "%.0f m");
            ImGui::SliderFloat("High-view bridge start", &config_.far_shell_start_m, 0.0F,
                               400000.0F, "%.0f m");
            ImGui::SliderFloat("High-view bridge end", &config_.far_shell_end_m, 1000.0F,
                               900000.0F, "%.0f m");
            ImGui::SliderFloat("High-view bridge strength", &config_.far_shell_strength, 0.0F,
                               1.5F, "%.2f");
            config_.orbit_transition_end_m =
                std::max(config_.orbit_transition_end_m, config_.orbit_transition_start_m + 1.0F);
            config_.far_shell_end_m =
                std::max(config_.far_shell_end_m, config_.far_shell_start_m + 1.0F);
        }
        if (ImGui::CollapsingHeader("Orbit Shell", ImGuiTreeNodeFlags_DefaultOpen)) {
            draw_enum_combo("Representation", config_.orbit_representation,
                            kCloudOrbitRepresentations, clouds_orbit_representation_name);
            ImGui::SliderFloat("Orbit detail", &config_.orbit_detail_strength, 0.0F, 1.0F,
                               "%.2f");
            ImGui::SliderFloat("Orbit density", &config_.orbit_density_scale, 0.0F, 2.0F,
                               "%.2f");
            ImGui::SliderFloat("Orbit empty-space fill", &config_.orbit_fill, 0.0F, 2.0F,
                               "%.2f");
            ImGui::SliderFloat("Orbit motion", &config_.orbit_motion_strength, 0.0F, 4.0F,
                               "%.2f");
            ImGui::SliderFloat("Orbit extinction", &config_.orbit_shell_extinction, 0.0F, 8.0F,
                               "%.2f");
        }
        if (ImGui::CollapsingHeader("Shape / Density", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Crispiness", &config_.crispiness, 1.0F, 120.0F, "%.1f");
            ImGui::SliderFloat("Curliness", &config_.curliness, 0.01F, 3.0F, "%.2f");
            ImGui::SliderFloat("Vertical shear", &config_.vertical_shear_fraction, 0.0F, 0.5F,
                               "%.2f");
            ImGui::SliderFloat("Weather softness", &config_.weather_softness, 0.02F, 0.60F,
                               "%.2f");
            ImGui::SliderFloat("Authored weather influence", &config_.weather_influence, 0.0F,
                               1.0F, "%.2f");
            ImGui::SliderFloat("Detail erosion", &config_.detail_erosion, 0.0F, 1.0F, "%.2f");
            ImGui::Checkbox("Powder", &config_.powder_enabled);
        }
        if (ImGui::CollapsingHeader("Lighting / Horizon Fill",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Ambient", &config_.ambient_strength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Direct", &config_.direct_strength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Phase / rim", &config_.phase_strength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Absorption", &config_.absorption, 0.0F, 1.5F, "%.2f");
            ImGui::SliderFloat("Shadow strength", &config_.shadow_strength, 0.0F, 2.0F, "%.2f");
            ImGui::SliderFloat("Horizon fill", &config_.horizon_strength, 0.0F, 2.0F, "%.2f");
        }
        if (ImGui::CollapsingHeader("Final Resolve / Sky Glow",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Resolve", &config_.resolve_strength, 0.0F, 1.0F, "%.2f");
            ImGui::SliderFloat("Contrast", &config_.final_contrast, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Saturation", &config_.final_saturation, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("Horizon glow", &config_.horizon_glow_strength, 0.0F, 3.0F,
                               "%.2f");
            ImGui::SliderFloat("Sun glare", &config_.sun_glare_strength, 0.0F, 3.0F, "%.2f");
        }
        ImGui::Separator();
        ImGui::Text("FPS: %.1f / %.2f ms", latest_fps_, latest_frame_ms_);
        ImGui::Text("Base noise: %u^3", kCloudBaseNoiseSize);
        ImGui::Text("Detail noise: %u^3", kCloudDetailNoiseSize);
        ImGui::Text("Weather texture: %u x %u", kCloudWeatherTextureSize,
                    kCloudWeatherTextureSize);
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
        cloud_runtime_.invalidate_history();
    }

    void create_global_resources(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu) {
        if (cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.create_generated_resources(device, gpu, cloud_runtime_shader_files().generated,
                                                  cloud_layer_config(config_, elapsed_seconds_));
        cloud_global_resources_created_ = true;
    }

    void destroy_global_resources() {
        cloud_runtime_.destroy_generated_resources();
        cloud_global_resources_created_ = false;
    }

    void refresh_weather_texture_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu) {
        if (!cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.update_weather_texture(device, gpu,
                                              cloud_runtime_shader_files().generated.weather,
                                              cloud_layer_config(config_, elapsed_seconds_));
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        cloud_runtime_.destroy_swapchain_resources();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count) {
        graph_executor_.resize(frame_slot_count);
        cloud_runtime_.create_swapchain_resources(
            device, cloud_runtime_shader_files(), cubey::render::CloudLayerCompositeMode::Standalone,
            color_format, extent, frame_slot_count, cloud_layer_config(config_, elapsed_seconds_));
    }

    [[nodiscard]] CloudFrameUniforms current_frame_uniforms(VkExtent2D target_extent) const {
        return cloud_frame_uniforms(config_, target_extent, yaw_, pitch_, elapsed_seconds_,
                                    cloud_runtime_.temporal_frame_index());
    }

    [[nodiscard]] CloudFrameGraph build_frame_graph(cubey::render::ColorTargetView target,
                                                       cubey::render::FrameSlot frame_slot,
                                                       CloudTargetMode mode,
                                                       CloudFrameUniforms uniforms) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer =
            graph.import_color_target("backbuffer", target, cloud_target_initial_state(mode),
                                      cloud_target_final_state(mode));
        cubey::render::CloudLayerRuntimeFrame cloud_frame = cloud_runtime_.declare_product(
            graph, target.extent, cloud_layer_config(config_, elapsed_seconds_), frame_slot,
            uniforms);
        cloud_runtime_.declare_composite(graph, backbuffer, cloud_frame, frame_slot);
        return {
            .graph = graph.compile(),
            .cloud = cloud_frame,
        };
    }

    void record_target(cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                       const cubey::render::ColorTargetView& target,
                       cubey::render::FrameSlot frame_slot, CloudTargetMode mode,
                       cubey::render::RenderGraphCommandBufferMode command_buffer_mode,
                       const char* label) {
        const CloudFrameUniforms uniforms = current_frame_uniforms(target.extent);
        cloud_runtime_.upload_frame_uniforms(frame_slot, uniforms);
        const CloudFrameGraph frame_graph = build_frame_graph(target, frame_slot, mode, uniforms);
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
                cloud_runtime_.update_descriptors(device, frame_slot, frame_graph.graph,
                                                  graph_resources, frame_graph.cloud);
            });
        cloud_runtime_.complete_frame(frame_slot, frame_graph.cloud);
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
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
    cubey::render::CloudLayerRuntime cloud_runtime_{};
    bool cloud_global_resources_created_ = false;
};

} // namespace

int run_cloud(const RunConfig& config) {
    CloudApp app(config);
    return app.run();
}

} // namespace cubey::projects::cloud
