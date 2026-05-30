#include "atmosphere_app.h"

#include "atmosphere_config.h"
#include "atmosphere_ui.h"

#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_ATMOSPHERE_SHADER_DIR
#error "CUBEY_ATMOSPHERE_SHADER_DIR must be defined by the atmosphere CMake target"
#endif

namespace cubey::projects::atmosphere {
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kBaseYaw = 0.0F;
constexpr float kBasePitch = 0.0F;
constexpr float kDefaultFovyRadians = 65.0F * (std::numbers::pi_v<float> / 180.0F);

struct AtmospherePushConstants {
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward_debug_view;
    cubey::math::Vec4 radii_ground;
    cubey::math::Vec4 rayleigh;
    cubey::math::Vec4 mie;
    cubey::math::Vec4 ozone;
    cubey::math::Vec4 sun_direction_radius;
    cubey::math::Vec4 display_transform;
    cubey::math::Vec4 atmosphere_options;
    cubey::math::Vec4 night_options;
    cubey::math::Vec4 celestial_options;
    cubey::math::Vec4 moon_direction_radius;
    cubey::math::Vec4 moon_options;
    cubey::math::Vec4 moon_phase_options;
};

static_assert(sizeof(AtmospherePushConstants) == sizeof(float) * 60U);

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_ATMOSPHERE_SHADER_DIR) / filename;
}

[[nodiscard]] float radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

[[nodiscard]] cubey::math::Vec3 sun_direction(const AtmosphereConfig& config) {
    const float elevation = radians(config.sun_elevation_degrees);
    const float azimuth = radians(config.sun_azimuth_degrees);
    const float horizontal = std::cos(elevation);
    return glm::normalize(cubey::math::Vec3{
        horizontal * std::sin(azimuth),
        std::sin(elevation),
        -horizontal * std::cos(azimuth),
    });
}

[[nodiscard]] cubey::render::MaterialPassInfo atmosphere_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(AtmospherePushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "atmosphere.fullscreen",
        .push_constants = {push_constant_range},
    };
}

class AtmosphereApp {
  public:
    explicit AtmosphereApp(RunConfig config)
        : run_config_(std::move(config)),
          atmosphere_config_(atmosphere_config_from_run_config(run_config_)),
          render_view_(atmosphere_config_.render_view),
          headless_base_time_hours_(atmosphere_config_.time_of_day.time_hours),
          headless_base_day_of_year_(atmosphere_config_.time_of_day.day_of_year) {
        view_controller_.set_auto_rotation_speed(0.0F);
    }

    AtmosphereApp(const AtmosphereApp&) = delete;
    AtmosphereApp& operator=(const AtmosphereApp&) = delete;

    ~AtmosphereApp() {
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
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) {
            draw_atmosphere_ui({
                .config = atmosphere_config_,
                .latest_frame_stats = latest_frame_stats_,
                .render_view = render_view_,
                .reset_requested = reset_requested_,
                .latest_fps = latest_fps_,
                .latest_frame_ms = latest_frame_ms_,
            });
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext&,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(frame);
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
                .run_config = run_config_,
                .app_name = "atmosphere",
                .ready_status = "rendering atmosphere project",
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
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
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
            update_headless_time(frame);
            record_atmosphere_target(command_buffer, target);
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
            reset_requested_ = true;
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            render_view_ = next_atmosphere_render_view(render_view_);
        }
        if (input.key_pressed(cubey::input::Key::Space) &&
            atmosphere_config_.time_of_day.mode == SunControlMode::SolarClock) {
            atmosphere_config_.time_of_day.playing = !atmosphere_config_.time_of_day.playing;
        }
        view_controller_.update_pointer_input(input, timing.delta_seconds);
        if (reset_requested_) {
            const AtmospherePreset preset = atmosphere_config_.preset;
            atmosphere_config_ = atmosphere_config_for_preset(preset);
            headless_base_time_hours_ = atmosphere_config_.time_of_day.time_hours;
            headless_base_day_of_year_ = atmosphere_config_.time_of_day.day_of_year;
            render_view_ = atmosphere_config_.render_view;
            view_controller_.reset();
            reset_requested_ = false;
        }
        advance_atmosphere_time_of_day(atmosphere_config_, timing.delta_seconds);
        resolve_atmosphere_time_of_day(atmosphere_config_);
        atmosphere_config_.render_view = render_view_;
        validate_atmosphere_config(atmosphere_config_);
    }

    void update_headless_time(const cubey::host::HeadlessCaptureFrame& frame) {
        if (run_config_.capture_mode == CaptureMode::Video &&
            atmosphere_config_.time_of_day.mode == SunControlMode::SolarClock &&
            atmosphere_config_.time_of_day.playing) {
            set_atmosphere_time_from_elapsed(atmosphere_config_.time_of_day,
                                             headless_base_time_hours_, headless_base_day_of_year_,
                                             frame.timing.elapsed_seconds);
        }
        resolve_atmosphere_time_of_day(atmosphere_config_);
        validate_atmosphere_config(atmosphere_config_);
    }

    std::optional<FrameStatsSample> record_frame_stats(VkExtent2D extent,
                                                       const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = 1,
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = shader_path("atmosphere.vert.spv"),
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = shader_path("atmosphere.frag.spv"),
            },
        };

        pipeline_resource_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                               .extent = extent,
                                               .color_format = color_format,
                                               .shader_stage_files = shader_stage_files,
                                               .material_pass = atmosphere_pass_info(),
                                           });
        pipeline_color_format_ = color_format;
    }

    void destroy_swapchain_resources() {
        pipeline_resource_.reset();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    }

    [[nodiscard]] AtmospherePushConstants push_constants(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float tan_half_fovy = std::tan(kDefaultFovyRadians * 0.5F);
        const cubey::math::Quat rotation =
            cubey::math::angle_axis_quat(kBaseYaw + view_controller_.yaw(), {0.0F, 1.0F, 0.0F}) *
            cubey::math::angle_axis_quat(kBasePitch + view_controller_.pitch(), {1.0F, 0.0F, 0.0F});
        const cubey::math::Vec3 right =
            glm::normalize(rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F});
        const cubey::math::Vec3 up = glm::normalize(rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F});
        const cubey::math::Vec3 forward =
            glm::normalize(rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
        const cubey::math::Vec3 sun = sun_direction(atmosphere_config_);
        const float sidereal_angle =
            atmosphere_sidereal_angle_radians(atmosphere_config_.time_of_day);
        const float latitude = radians(atmosphere_config_.time_of_day.latitude_degrees);
        const LunarState lunar_state =
            atmosphere_lunar_state(atmosphere_config_.time_of_day, atmosphere_config_.moon);
        const cubey::render::PbrDisplayTransform display_transform =
            cubey::render::pbr_display_transform_for_target(pipeline_color_format_,
                                                            atmosphere_config_.exposure);

        return {
            .camera_right_aspect = {right.x, right.y, right.z, aspect},
            .camera_up_tan_half_fovy = {up.x, up.y, up.z, tan_half_fovy},
            .camera_forward_debug_view =
                {
                    forward.x,
                    forward.y,
                    forward.z,
                    static_cast<float>(static_cast<std::uint32_t>(render_view_)),
                },
            .radii_ground =
                {
                    atmosphere_config_.bottom_radius_km,
                    atmosphere_config_.top_radius_km,
                    atmosphere_config_.camera_altitude_km,
                    atmosphere_config_.ground_albedo,
                },
            .rayleigh =
                {
                    atmosphere_config_.rayleigh_scattering.x *
                        atmosphere_config_.rayleigh_density_scale,
                    atmosphere_config_.rayleigh_scattering.y *
                        atmosphere_config_.rayleigh_density_scale,
                    atmosphere_config_.rayleigh_scattering.z *
                        atmosphere_config_.rayleigh_density_scale,
                    atmosphere_config_.rayleigh_scale_height_km,
                },
            .mie =
                {
                    atmosphere_config_.mie_scattering * atmosphere_config_.mie_density_scale,
                    atmosphere_config_.mie_extinction * atmosphere_config_.mie_density_scale,
                    atmosphere_config_.mie_scale_height_km,
                    atmosphere_config_.mie_anisotropy,
                },
            .ozone =
                {
                    atmosphere_config_.ozone_absorption.x,
                    atmosphere_config_.ozone_absorption.y,
                    atmosphere_config_.ozone_absorption.z,
                    atmosphere_config_.ozone_center_altitude_km,
                },
            .sun_direction_radius =
                {
                    sun.x,
                    sun.y,
                    sun.z,
                    atmosphere_config_.sun_angular_radius,
                },
            .display_transform = cubey::render::pbr_display_transform_uniform(display_transform),
            .atmosphere_options =
                {
                    atmosphere_config_.ozone_half_width_km,
                    atmosphere_config_.reference_geometry_enabled ? 1.0F : 0.0F,
                    atmosphere_config_.reference_grid_km,
                    atmosphere_config_.reference_intensity,
                },
            .night_options =
                {
                    atmosphere_config_.night_sky.twilight_strength,
                    atmosphere_config_.night_sky.twilight_horizon_warmth,
                    atmosphere_config_.night_sky.star_intensity,
                    atmosphere_config_.night_sky.star_density,
                },
            .celestial_options =
                {
                    std::cos(sidereal_angle),
                    std::sin(sidereal_angle),
                    std::sin(latitude),
                    std::cos(latitude),
                },
            .moon_direction_radius =
                {
                    lunar_state.direction.x,
                    lunar_state.direction.y,
                    lunar_state.direction.z,
                    lunar_state.angular_radius,
                },
            .moon_options =
                {
                    atmosphere_config_.moon.enabled ? 1.0F : 0.0F,
                    atmosphere_config_.moon.disk_intensity,
                    atmosphere_config_.moon.moonlight_intensity,
                    lunar_state.illumination,
                },
            .moon_phase_options =
                {
                    lunar_state.phase_fraction,
                    std::sin(lunar_state.phase_fraction * 2.0F * std::numbers::pi_v<float>),
                    0.0F,
                    0.0F,
                },
        };
    }

    void record_atmosphere_draw(const cubey::vulkan::CommandRecorder& recorder,
                                const cubey::render::ColorTargetView& target) const {
        const AtmospherePushConstants constants = push_constants(target.extent);
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

    void record_windowed_frame(const cubey::host::WindowedRenderFrame& frame) {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        cubey::render::record_present_render_target(
            recorder, cubey::render::render_target_view(frame.color_target),
            [this, &frame](const cubey::vulkan::CommandRecorder& present_recorder) {
                record_atmosphere_draw(present_recorder, frame.color_target);
            });
        recorder.end("vkEndCommandBuffer atmosphere");
    }

    void record_atmosphere_target(VkCommandBuffer command_buffer,
                                  const cubey::host::HeadlessRenderTarget& target) {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        record_atmosphere_draw(recorder, target);
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline_resource() const {
        if (!pipeline_resource_.has_value()) {
            throw std::runtime_error("atmosphere pipeline resource is not initialized");
        }
        return pipeline_resource_.value();
    }

    RunConfig run_config_;
    AtmosphereConfig atmosphere_config_;
    AtmosphereRenderView render_view_ = AtmosphereRenderView::Final;
    cubey::OrbitController view_controller_{cubey::OrbitControllerConfig{
        .distance = 1.0F,
        .min_distance = 1.0F,
        .max_distance = 1.0F,
    }};
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    float headless_base_time_hours_ = 12.0F;
    float headless_base_day_of_year_ = 80.0F;
    bool reset_requested_ = false;

    VkFormat pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_resource_;
};

} // namespace

int run_atmosphere(const RunConfig& config) {
    AtmosphereApp app(config);
    return app.run();
}

} // namespace cubey::projects::atmosphere
