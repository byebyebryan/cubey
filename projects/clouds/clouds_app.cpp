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
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

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

#ifndef CUBEY_CLOUDS_SHADER_DIR
#error "CUBEY_CLOUDS_SHADER_DIR must be defined by the clouds CMake target"
#endif

namespace cubey::projects::clouds {
namespace {

constexpr float kDefaultFovyRadians = 60.0F * (glm::pi<float>() / 180.0F);
constexpr std::array<CloudsCameraMode, 3> kCloudsCameraModes{
    CloudsCameraMode::Surface,
    CloudsCameraMode::High,
    CloudsCameraMode::Orbit,
};
constexpr std::array<CloudsQuality, 3> kCloudsQualityModes{
    CloudsQuality::Quarter,
    CloudsQuality::Half,
    CloudsQuality::Full,
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

struct CloudsViewBasis {
    cubey::math::Vec3 position_km{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, -1.0F};
};

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_CLOUDS_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo clouds_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(CloudsPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "clouds.fullscreen",
        .push_constants = {push_constant_range},
    };
}

[[nodiscard]] cubey::math::Vec3 safe_normalize(cubey::math::Vec3 value,
                                               cubey::math::Vec3 fallback) {
    const float len2 = glm::dot(value, value);
    if (len2 <= 0.0000001F) {
        return fallback;
    }
    return value * glm::inversesqrt(len2);
}

[[nodiscard]] CloudsViewBasis clouds_view_basis(const CloudsConfig& config, float yaw,
                                                float pitch) {
    CloudsViewBasis basis{};
    const float altitude_km = config.camera_altitude_m * 0.001F;
    basis.position_km = {0.0F, altitude_km, 0.0F};

    if (config.camera_mode == CloudsCameraMode::Orbit) {
        const float orbit_pitch = std::clamp(pitch, -0.75F, 0.75F);
        const float radius_km = config.planet_radius_m * 0.001F + altitude_km;
        const cubey::math::Vec3 planet_center{0.0F, -config.planet_radius_m * 0.001F, 0.0F};
        const cubey::math::Vec3 radial{
            std::sin(yaw) * std::cos(orbit_pitch),
            std::cos(orbit_pitch),
            std::cos(yaw) * std::cos(orbit_pitch),
        };
        basis.position_km = planet_center + radial * radius_km;
        basis.forward = safe_normalize(planet_center - basis.position_km, {0.0F, -1.0F, 0.0F});
        basis.right = safe_normalize(glm::cross(basis.forward, cubey::math::Vec3{0.0F, 1.0F, 0.0F}),
                                     {1.0F, 0.0F, 0.0F});
        basis.up = safe_normalize(glm::cross(basis.right, basis.forward), {0.0F, 0.0F, 1.0F});
        return basis;
    }

    const float base_pitch = config.camera_mode == CloudsCameraMode::High ? -0.92F : 0.22F;
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
    const float sun_intensity = std::clamp((solar_position.elevation_degrees + 8.0F) / 28.0F,
                                           0.04F, 1.0F);

    return {
        .camera_right_aspect = {basis.right.x, basis.right.y, basis.right.z, aspect},
        .camera_up_tan_half_fovy = {basis.up.x, basis.up.y, basis.up.z, tan_half_fovy},
        .camera_forward_mode = {basis.forward.x, basis.forward.y, basis.forward.z,
                                static_cast<float>(static_cast<std::uint32_t>(config.camera_mode))},
        .camera_position_radius = {basis.position_km.x, basis.position_km.y, basis.position_km.z,
                                   config.planet_radius_m * 0.001F},
        .cloud_shell = {config.bottom_altitude_m * 0.001F, config.top_altitude_m * 0.001F,
                        100.0F, budget.resolution_scale},
        .weather = {config.coverage, config.density, config.weather_scale_km,
                    elapsed_seconds * config.wind_speed_mps * 0.001F},
        .sun_direction_intensity = {sun_direction.x, sun_direction.y, sun_direction.z,
                                    sun_intensity},
        .render_options = {static_cast<float>(static_cast<std::uint32_t>(config.debug_view)),
                           static_cast<float>(budget.view_steps),
                           static_cast<float>(budget.light_steps), config.time.time_hours},
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
                            context.swapchain().extent());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_windowed(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) {
            draw_ui(context);
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext&,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(frame.command_buffer, frame.color_target);
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
            create_pipeline(context.device(), target.format, target.extent);
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext&,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            elapsed_seconds_ = static_cast<float>(frame.index) / static_cast<float>(run_config_.fps);
            CloudsConfig frame_config = config_;
            advance_clouds_time(frame_config, elapsed_seconds_);
            config_ = frame_config;
            record_headless_target(command_buffer, target);
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
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            config_.debug_view = next_clouds_debug_view(config_.debug_view);
        }
        if (input.key_pressed(cubey::input::Key::Space)) {
            config_.time.playing = !config_.time.playing;
        }
        if (input.mouse_enabled() && input.mouse_button_down(cubey::input::MouseButton::Left)) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Left);
            yaw_ -= static_cast<float>(delta.x) * 0.006F;
            pitch_ = std::clamp(pitch_ - static_cast<float>(delta.y) * 0.006F, -1.2F, 1.2F);
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
        }

        cubey::host::imgui_enum_combo(
            "Debug view", config_.debug_view, kCloudsDebugViews, clouds_debug_view_name,
            "Inspect final color, weather, density, transmittance, lighting, shadow, or step use.");
        cubey::host::imgui_enum_combo(
            "Quality", config_.quality, kCloudsQualityModes, clouds_quality_name,
            "Controls raymarch view and light sample budgets.");

        if (const cubey::host::ScopedImGuiGroup group{
                "Camera", {.help = "Camera preset and orbit/surface framing controls."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Camera");
            CloudsCameraMode selected_mode = config_.camera_mode;
            if (cubey::host::imgui_enum_combo("Mode", selected_mode, kCloudsCameraModes,
                                              clouds_camera_mode_name,
                                              "Surface, high-altitude, and orbit inspection "
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
            cubey::host::imgui_slider_float("Coverage", &config_.coverage, 0.0F, 1.0F, "%.2f",
                                            "Amount of sky covered by the procedural weather map.");
            cubey::host::imgui_slider_float("Density", &config_.density, 0.0F, 2.0F, "%.2f",
                                            "Optical density multiplier for the cloud volume.");
            cubey::host::imgui_slider_float("Scale", &config_.weather_scale_km, 20.0F, 500.0F,
                                            "%.0f km",
                                            "Horizontal scale of the broad weather field.");
            cubey::host::imgui_slider_float("Wind", &config_.wind_speed_mps, 0.0F, 80.0F,
                                            "%.1f m/s",
                                            "Advection speed for procedural cloud features.");
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Layer", {.default_open = false,
                           .help = "Cloud-shell altitude range above the planet surface."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Layer");
            ImGui::InputFloat("Bottom", &config_.bottom_altitude_m, 0.0F, 0.0F, "%.0f");
            cubey::host::imgui_attach_help("Cloud layer bottom altitude in meters.");
            ImGui::InputFloat("Top", &config_.top_altitude_m, 0.0F, 0.0F, "%.0f");
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
            ImGui::TextUnformatted("Keys: D debug, Space solar play, R reset");
        }

        const CloudsQualityBudget budget = clouds_quality_budget(config_.quality);
        const std::array<cubey::host::PerformanceCounter, 5> performance_counters{
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
                "Quality scale", static_cast<std::uint64_t>(budget.resolution_scale * 100.0F),
                "%"},
            cubey::host::PerformanceCounter{"Fullscreen tris", 1, nullptr},
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
            .triangles = 1,
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
        yaw_ = 0.0F;
        pitch_ = 0.0F;
    }

    void destroy_swapchain_resources() {
        pipeline_resource_.reset();
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("clouds.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("clouds.frag.spv")),
        };
        const cubey::render::MaterialPassInfo material_pass = clouds_pass_info();
        pipeline_resource_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                               .extent = extent,
                                               .color_format = color_format,
                                               .shader_stage_files = shader_stage_files,
                                               .material_pass = material_pass,
                                           });
    }

    void record_clouds_draw(const cubey::vulkan::CommandRecorder& recorder,
                            const cubey::render::ColorTargetView& target) const {
        const CloudsPushConstants constants =
            clouds_push_constants(config_, target.extent, yaw_, pitch_, elapsed_seconds_);
        cubey::render::record_render_target_pass(
            recorder, cubey::render::render_target_view(target),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
            },
            [this, constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
                cubey::render::record_fullscreen_pipeline_draw(
                    pass_recorder, {.pipeline = &pipeline_resource()}, VK_SHADER_STAGE_FRAGMENT_BIT,
                    constants);
            });
    }

    void record_windowed_frame(VkCommandBuffer command_buffer,
                               const cubey::render::ColorTargetView& target) const {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        cubey::render::record_present_render_target(
            recorder, cubey::render::render_target_view(target),
            [this, &target](const cubey::vulkan::CommandRecorder& present_recorder) {
                record_clouds_draw(present_recorder, target);
            });
        recorder.end("vkEndCommandBuffer clouds");
    }

    void record_headless_target(VkCommandBuffer command_buffer,
                                const cubey::render::ColorTargetView& target) const {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        record_clouds_draw(recorder, target);
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline_resource() const {
        if (!pipeline_resource_.has_value()) {
            throw std::runtime_error("clouds pipeline resource is not initialized");
        }
        return pipeline_resource_.value();
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
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_resource_;
};

} // namespace

int run_clouds(const RunConfig& config) {
    CloudsApp app(config);
    return app.run();
}

} // namespace cubey::projects::clouds
