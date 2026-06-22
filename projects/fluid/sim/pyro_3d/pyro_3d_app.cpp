#include "pyro_3d_app.h"

#include "pyro_3d_commands.h"
#include "pyro_3d_config.h"
#include "pyro_3d_gpu_resources.h"
#include "pyro_3d_sources.h"

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/imgui_helpers.h>
#include <cubey/host/performance_ui.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/celestial_body_frame.h>
#include <cubey/render/celestial_system.h>
#include <cubey/render/color_space.h>
#include <cubey/render/environment_lighting.h>
#include <cubey/render/pass.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::pyro_3d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kFireCameraDistance = 2.05F;
constexpr float kExplosionCameraDistance = 2.18F;
constexpr float kCameraBaseYaw = cubey::render::kAtmosphereEnvironmentSunriseViewYawRadians;
constexpr float kCameraBasePitch = cubey::render::kAtmosphereEnvironmentSunriseViewPitchRadians;
constexpr float kHeadlessVideoOrbitSpeed = 0.32F;
constexpr cubey::math::Vec3 kVolumeCenter{0.5F, 0.5F, 0.5F};

[[nodiscard]] float default_camera_distance(Pyro3DMode mode) {
    switch (mode) {
    case Pyro3DMode::Fire:
        return kFireCameraDistance;
    case Pyro3DMode::Explosion:
        return kExplosionCameraDistance;
    }
    return kFireCameraDistance;
}

[[nodiscard]] const char* debug_view_name(Pyro3DDebugView view) {
    switch (view) {
    case Pyro3DDebugView::Smoke:
        return "Smoke";
    case Pyro3DDebugView::DensitySlice:
        return "Density slice";
    case Pyro3DDebugView::Velocity:
        return "Velocity";
    }
    return "Smoke";
}

[[nodiscard]] Pyro3DDebugView debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "smoke") {
        return Pyro3DDebugView::Smoke;
    }
    if (name == "density" || name == "density-slice") {
        return Pyro3DDebugView::DensitySlice;
    }
    if (name == "velocity") {
        return Pyro3DDebugView::Velocity;
    }
    throw std::runtime_error("pyro 3D debug view must be smoke, density-slice, or velocity");
}

[[nodiscard]] bool use_atmosphere_environment_source(const RunConfig& config) {
    return config.pbr.environment_source.empty() || config.pbr.environment_source == "atmosphere";
}

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
pyro_3d_atmosphere_run_state(const RunConfig& config) {
    return cubey::atmosphere_environment_run_state_from_config(
        config.atmosphere,
        {
            .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
            .reference_geometry_enabled = false,
        });
}

[[nodiscard]] cubey::math::Vec3 linear_rgb(std::array<float, 3> srgb) {
    const std::array<float, 3> linear = cubey::render::srgb_to_linear_rgb(srgb);
    return {linear[0], linear[1], linear[2]};
}

[[nodiscard]] cubey::render::EnvironmentLightingUniforms
pyro_3d_static_environment_lighting(float exposure) {
    return {
        .primary_light_direction_intensity = {0.45F, 0.82F, 0.35F, 1.0F},
        .primary_light_color_exposure =
            cubey::render::vec3_and_float(linear_rgb({1.0F, 0.92F, 0.80F}), exposure),
        .ambient_color_intensity =
            cubey::render::vec3_and_float(linear_rgb({0.42F, 0.54F, 0.76F}), 1.0F),
        .sky_color_options = cubey::render::vec3_and_float(linear_rgb({0.42F, 0.54F, 0.76F}), 0.0F),
    };
}

constexpr std::array<Pyro3DDebugView, 3> kDebugViews{
    Pyro3DDebugView::Smoke,
    Pyro3DDebugView::DensitySlice,
    Pyro3DDebugView::Velocity,
};

class Pyro3DApp {
  public:
    Pyro3DApp(RunConfig config, Pyro3DAppInfo app_info)
        : config_(std::move(config)), app_info_(app_info), runtime_(1),
          pyro_config_(pyro_3d_config_from_run_config(config_, app_info_.mode)),
          source_states_(create_pyro_3d_sources(pyro_config_)),
          source_gpu_(pyro_3d_sources_to_gpu(source_states_, pyro_config_)),
          atmosphere_state_(pyro_3d_atmosphere_run_state(config_)),
          debug_view_(debug_view_from_name(config_.debug_view)) {
        orbit_controller_.set_home_distance(default_camera_distance(app_info_.mode));
        orbit_controller_.set_auto_rotation_speed(0.12F);
    }

    Pyro3DApp(const Pyro3DApp&) = delete;
    Pyro3DApp& operator=(const Pyro3DApp&) = delete;

    int run() {
        if (config_.headless) {
            return run_headless();
        }

        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            const VkExtent2D extent = context.swapchain().extent();
            create_render_pipeline(context.device(), context.swapchain().format(), extent);
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            const ProjectFrame& project_frame = runtime_.frame_for_timing(timing);
            update_interaction(context, project_frame);
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            const ProjectFrame& project_frame = runtime_.frame_for_timing(frame.timing);
            record_frame(context, frame, project_frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            (void)context;
            destroy_all_resources();
            retire_project_gpu_work();
            detach_project_gpu();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = app_info_.app_name,
                .ready_status = app_info_.ready_status,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

  private:
    void update_interaction(cubey::host::WindowedAppContext& context,
                            const ProjectFrame& project_frame) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::Space)) {
            paused_ = !paused_;
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_simulation();
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            debug_view_ = next_debug_view(debug_view_);
        }

        update_camera_input(context, project_frame.delta_seconds);
        update_atmosphere_time(project_frame.delta_seconds);
        if (!paused_) {
            update_sources(project_frame);
        }
    }

    void update_camera_input(cubey::host::WindowedAppContext& context, double delta_seconds) {
        orbit_controller_.update_pointer_input(context.filtered_input(), delta_seconds);
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        if (!cubey::host::begin_control_panel(app_info_.ui_title)) {
            ImGui::End();
            return;
        }

        cubey::host::imgui_checkbox("Paused", &paused_, "Pause volume simulation time.");
        ImGui::SameLine();
        if (cubey::host::imgui_button("Reset", "Restart the volume simulation.")) {
            reset_simulation();
        }

        cubey::host::imgui_enum_combo("Debug view", debug_view_, kDebugViews, debug_view_name);

        ImGui::Text("Mode: %s", pyro_3d_mode_name(pyro_config_.mode));

        if (const cubey::host::ScopedImGuiGroup group{
                "Simulation",
                {.default_open = false,
                 .help = "Pressure projection, decay, buoyancy, and vorticity controls."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Simulation");
            cubey::host::imgui_slider_uint32("Pressure iterations",
                                             &pyro_config_.pressure_iterations, 1U, 48U);
            cubey::host::imgui_slider_float("Density decay", &pyro_config_.density_decay_per_second,
                                            0.960F, 1.0F, "%.4f");
            cubey::host::imgui_slider_float(
                "Velocity decay", &pyro_config_.velocity_decay_per_second, 0.960F, 1.0F, "%.4f");
            cubey::host::imgui_slider_float("Buoyancy", &pyro_config_.buoyancy_strength, -2.0F,
                                            6.0F, "%.2f");
            cubey::host::imgui_slider_float("Vorticity", &pyro_config_.vorticity_strength, 0.0F,
                                            1.5F, "%.2f");
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Sources", {.help = "Procedural fuel, heat, smoke, and velocity sources."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Sources");
            if (cubey::host::imgui_slider_uint32("Sources", &pyro_config_.source_count, 1U,
                                                 kMaxPyro3DSourceCount)) {
                reset_sources();
            }
            cubey::host::imgui_slider_float("Source height", &pyro_config_.source_center_height,
                                            0.020F, 0.500F, "%.3f");
            cubey::host::imgui_slider_float("Source radius", &pyro_config_.source_radius, 0.020F,
                                            0.180F, "%.3f");
            cubey::host::imgui_slider_float("Smoke", &pyro_config_.source_smoke_amount, 0.0F, 32.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Heat", &pyro_config_.source_heat_amount, 0.0F, 8.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float(pyro_config_.mode == Pyro3DMode::Fire ? "Fuel"
                                                                                  : "Flame",
                                            &pyro_config_.source_flame_amount, 0.0F, 12.0F, "%.2f");
            cubey::host::imgui_slider_float(
                "Velocity force", &pyro_config_.source_velocity_strength, 0.0F, 16.0F, "%.2f");
        }

        const char* model_section =
            pyro_config_.mode == Pyro3DMode::Fire ? "Fire model" : "Explosion model";
        if (const cubey::host::ScopedImGuiGroup group{
                model_section,
                {.default_open = false,
                 .help = "Mode-specific flame, combustion, and explosion shaping controls."}};
            group) {
            const cubey::host::ScopedImGuiId section_id(model_section);
            if (pyro_config_.mode == Pyro3DMode::Explosion) {
                cubey::host::imgui_slider_float("Explosion interval",
                                                &pyro_config_.explosion_interval_seconds, 0.25F,
                                                8.0F, "%.2f");
                const float max_duration = std::min(pyro_config_.explosion_interval_seconds, 1.0F);
                pyro_config_.explosion_duration_seconds =
                    std::min(pyro_config_.explosion_duration_seconds, max_duration);
                cubey::host::imgui_slider_float("Explosion duration",
                                                &pyro_config_.explosion_duration_seconds, 0.016F,
                                                max_duration, "%.3f");
                cubey::host::imgui_slider_float("Explosion boost", &pyro_config_.explosion_boost,
                                                0.0F, 40.0F, "%.2f");
            }
            if (pyro_config_.mode == Pyro3DMode::Fire) {
                cubey::host::imgui_slider_float("Ignition", &pyro_config_.fire_ignition_temperature,
                                                0.0F, 2.0F, "%.2f");
                cubey::host::imgui_slider_float("Burn rate", &pyro_config_.fire_burn_rate, 0.0F,
                                                10.0F, "%.2f");
                cubey::host::imgui_slider_float("Heat output", &pyro_config_.fire_heat_output, 0.0F,
                                                8.0F, "%.2f");
                cubey::host::imgui_slider_float("Soot yield", &pyro_config_.fire_soot_yield, 0.0F,
                                                1.5F, "%.2f");
            }
            cubey::host::imgui_slider_float("Expansion", &pyro_config_.fire_expansion, 0.0F, 4.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Flame cooling", &pyro_config_.fire_flame_cooling, 0.0F,
                                            8.0F, "%.2f");
            cubey::host::imgui_slider_float("Shredding", &pyro_config_.fire_shredding, 0.0F, 8.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Turbulence", &pyro_config_.fire_turbulence, 0.0F, 3.0F,
                                            "%.2f");
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Obstacles",
                {.default_open = false, .help = "Solid obstacle placement for the volume solve."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Obstacles");
            cubey::host::imgui_slider_float("Obstacle height", &pyro_config_.obstacle_center_height,
                                            0.0F, 1.0F, "%.2f");
            cubey::host::imgui_slider_float("Obstacle radius", &pyro_config_.obstacle_radius, 0.0F,
                                            kMaxPyro3DObstacleRadius, "%.3f");
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Rendering",
                {.default_open = false,
                 .help = "Raymarch, exposure, flame, smoke, and backdrop controls."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Rendering");
            cubey::host::imgui_slider_uint32("Raymarch steps", &pyro_config_.raymarch_steps, 24U,
                                             192U);
            cubey::host::imgui_slider_float("Exposure", &pyro_config_.render_exposure, -2.0F, 2.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Backdrop", &pyro_config_.render_background_lift, 0.0F,
                                            1.0F, "%.2f");
            cubey::host::imgui_slider_float("Absorption", &pyro_config_.absorption, 0.5F, 48.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Light", &pyro_config_.emission, 0.1F, 4.0F, "%.2f");
            cubey::host::imgui_slider_float("Ambient", &pyro_config_.ambient_light, 0.0F, 1.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Rim", &pyro_config_.render_rim_strength, 0.0F, 3.0F,
                                            "%.2f");
            cubey::host::imgui_slider_float("Scatter", &pyro_config_.render_scatter_strength, 0.0F,
                                            3.0F, "%.2f");
            cubey::host::imgui_slider_float("Smoke warmth", &pyro_config_.render_smoke_warmth, 0.0F,
                                            1.0F, "%.2f");
            cubey::host::imgui_slider_float("Flame intensity", &pyro_config_.render_flame_intensity,
                                            0.0F, 3.0F, "%.2f");
            cubey::host::imgui_slider_float("Flame core", &pyro_config_.render_flame_core_strength,
                                            0.0F, 3.0F, "%.2f");
        }

        if (use_atmosphere_environment_source()) {
            if (cubey::host::draw_atmosphere_environment_controls(
                    atmosphere_state_,
                    {.label = "Environment",
                     .default_open = false,
                     .help = "Shared procedural atmosphere driving pyro light, shadows, backdrop, "
                             "and exposure."})) {
                refresh_atmosphere_environment();
            }
        }

        if (const cubey::host::ScopedImGuiGroup group{
                "Shadows",
                {.default_open = false, .help = "Volume shadow marching and update cadence."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Shadows");
            cubey::host::imgui_slider_float("Shadow", &pyro_config_.shadow_absorption, 0.0F, 96.0F,
                                            "%.2f");
            cubey::host::imgui_slider_uint32("Shadow steps", &pyro_config_.shadow_steps, 8U, 192U);
            cubey::host::imgui_slider_uint32("Shadow interval",
                                             &pyro_config_.shadow_update_interval, 1U, 8U);
        }

        const std::array<cubey::host::PerformanceCounter, 3> performance_counters{
            cubey::host::PerformanceCounter{
                "Grid voxels",
                static_cast<std::uint64_t>(pyro_config_.grid_width) *
                    static_cast<std::uint64_t>(pyro_config_.grid_height) *
                    static_cast<std::uint64_t>(pyro_config_.grid_depth),
                nullptr},
            cubey::host::PerformanceCounter{
                "Shadow voxels",
                static_cast<std::uint64_t>(pyro_config_.shadow_grid_width) *
                    static_cast<std::uint64_t>(pyro_config_.shadow_grid_height) *
                    static_cast<std::uint64_t>(pyro_config_.shadow_grid_depth),
                nullptr},
            cubey::host::PerformanceCounter{"Sources", pyro_config_.source_count, nullptr},
        };
        cubey::host::draw_performance_ui({
            .frame_stats = latest_frame_stats_,
            .latest_fps = latest_fps_,
            .latest_frame_ms = latest_frame_ms_,
            .process = process_stats_.sample(),
            .device_memory_budget = context.device().device_memory_budget(),
            .counters = performance_counters,
            .gpu_timings = resources_.latest_timings(),
        });

        if (const cubey::host::ScopedImGuiGroup group{
                "Diagnostics",
                {.default_open = false, .help = "Read-only grid and GPU timing statistics."}};
            group) {
            const cubey::host::ScopedImGuiId section_id("Diagnostics");
            ImGui::Text("Grid: %u x %u x %u", pyro_config_.grid_width, pyro_config_.grid_height,
                        pyro_config_.grid_depth);
            ImGui::Text("Shadow grid: %u x %u x %u", pyro_config_.shadow_grid_width,
                        pyro_config_.shadow_grid_height, pyro_config_.shadow_grid_depth);
        }
        ImGui::End();
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

    void reset_sources() {
        source_states_ = create_pyro_3d_sources(pyro_config_);
        source_gpu_ = pyro_3d_sources_to_gpu(source_states_, pyro_config_);
    }

    void reset_simulation() {
        reset_sources();
        reset_requested_ = true;
        frame_state_.density_a_current = true;
        frame_state_.velocity_a_current = true;
        frame_state_.shadow_initialized = false;
        frame_state_.frames_since_shadow_update = 0;
    }

    [[nodiscard]] bool use_atmosphere_environment_source() const {
        return cubey::projects::fluid::pyro_3d::use_atmosphere_environment_source(config_);
    }

    void refresh_atmosphere_environment() {
        cubey::atmosphere_environment_resolve_run_state(atmosphere_state_);
        frame_state_.shadow_initialized = false;
        frame_state_.frames_since_shadow_update = 0;
    }

    void update_atmosphere_time(double delta_seconds) {
        if (!use_atmosphere_environment_source()) {
            return;
        }
        if (cubey::atmosphere_environment_advance_time(atmosphere_state_, delta_seconds)) {
            refresh_atmosphere_environment();
        }
    }

    [[nodiscard]] float resolved_render_exposure() const {
        if (!use_atmosphere_environment_source()) {
            return pyro_config_.render_exposure;
        }
        const float environment_exposure =
            atmosphere_state_.auto_exposure_enabled && !config_.pbr.exposure_explicit
                ? atmosphere_state_.resolved_exposure
                : config_.pbr.exposure;
        return environment_exposure + pyro_config_.render_exposure;
    }

    [[nodiscard]] Pyro3DConfig render_config() const {
        Pyro3DConfig result = pyro_config_;
        result.render_exposure = resolved_render_exposure();
        return result;
    }

    [[nodiscard]] cubey::render::EnvironmentLightingUniforms environment_lighting_uniforms() const {
        if (!use_atmosphere_environment_source()) {
            return pyro_3d_static_environment_lighting(pyro_config_.render_exposure);
        }
        const cubey::render::AtmosphereEnvironmentLighting lighting =
            cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment);
        return cubey::render::environment_lighting_uniforms(lighting, resolved_render_exposure());
    }

    [[nodiscard]] std::optional<cubey::render::AtmosphereBackgroundTextureBindings>
    atmosphere_background_bindings() const {
        if (!use_atmosphere_environment_source() || !atmosphere_background_atlases_.has_value()) {
            return std::nullopt;
        }
        return atmosphere_background_atlases_->bindings();
    }

    void update_sources(const ProjectFrame& frame) {
        const FrameTiming timing{
            .delta_seconds = frame.delta_seconds,
            .elapsed_seconds = frame.elapsed_seconds,
            .frame_index = frame.frame_index,
        };
        source_gpu_ = update_pyro_3d_sources(source_states_, pyro_config_, timing);
    }

    [[nodiscard]] cubey::Transform3D render_camera_transform() const {
        return cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = kVolumeCenter,
            .distance = orbit_controller_.distance(),
            .yaw = kCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = kCameraBasePitch + orbit_controller_.pitch(),
        });
    }

    [[nodiscard]] Pyro3DRenderCamera render_camera() const {
        const cubey::Transform3D transform = render_camera_transform();
        const cubey::math::Quat rotation = transform.rotation;
        return {
            .position = transform.translation,
            .right = rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F},
            .up = rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F},
            .forward = rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F},
            .fovy_radians = camera_.fovy_radians(),
        };
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_background_uniforms(const Pyro3DRenderCamera& camera, VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::render::ViewRayBasis3D view_rays{
            .right_aspect = {camera.right.x, camera.right.y, camera.right.z, aspect},
            .up_tan_half_fovy = {camera.up.x, camera.up.y, camera.up.z,
                                 std::tan(camera.fovy_radians * 0.5F)},
            .forward = {camera.forward.x, camera.forward.y, camera.forward.z, 0.0F},
        };
        cubey::render::AtmosphereEnvironmentConfig environment = atmosphere_state_.environment;
        environment.render_moon_disk = false;
        return cubey::render::atmosphere_environment_frame_uniforms(
            environment, {
                             .view_rays = view_rays,
                             .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
                         });
    }

    [[nodiscard]] bool moon_body_render_enabled() const {
        const cubey::render::AtmosphereEnvironmentConfig& environment =
            atmosphere_state_.environment;
        return use_atmosphere_environment_source() && environment.render_celestial_content &&
               environment.render_moon_disk && environment.moon.enabled;
    }

    [[nodiscard]] cubey::render::CelestialBodyFrameUniforms
    moon_body_frame_uniforms(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::Transform3D transform = render_camera_transform();
        const cubey::render::AtmosphereEnvironmentConfig& environment =
            atmosphere_state_.environment;
        const cubey::render::AtmosphereEnvironmentLunarState lunar =
            cubey::render::atmosphere_environment_lunar_state(environment.time_of_day,
                                                              environment.moon);
        const cubey::render::AtmosphereEnvironmentLighting atmosphere_lighting =
            cubey::render::atmosphere_environment_lighting(environment);
        cubey::render::CelestialBody moon{};
        moon.type = cubey::render::CelestialBodyType::Moon;
        moon.visible = moon_body_render_enabled();
        moon.direction = lunar.direction;
        moon.color = {0.58F, 0.62F, 0.74F};
        moon.intensity = environment.moon.disk_intensity;
        moon.angular_radius_rad = lunar.angular_radius;
        moon.distance_m = 384400000.0F;
        moon.radius_m = 1737400.0F;
        moon.phase_fraction = lunar.phase_fraction;

        const cubey::render::CelestialBodyRenderPlacement placement =
            cubey::render::celestial_body_render_placement(
                moon, {
                          .camera_render_position_m = transform.translation,
                          .near_plane_m = camera_.near_z(),
                          .far_plane_m = camera_.far_z(),
                          .angular_radius_scale = 1.0F,
                          .shell_distance_fraction = 0.62F,
                      });
        const cubey::render::CelestialLighting lighting{
            .primary_light_direction = atmosphere_lighting.sun_direction,
            .primary_light_color = atmosphere_lighting.sun_color,
            .primary_light_intensity = environment.moon.disk_intensity,
            .primary_light_angular_radius_rad = environment.sun_angular_radius,
            .moon_light_direction = lunar.direction,
            .moon_light_color = atmosphere_lighting.moon_color,
            .moon_light_intensity = atmosphere_lighting.moon_intensity,
        };
        return cubey::render::celestial_body_frame_uniforms(
            moon, placement, lighting, camera_.view_projection_matrix(transform, aspect),
            {
                .camera_render_position_m = transform.translation,
            });
    }

    void destroy_swapchain_resources() {
        resources_.destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        resources_.destroy_all_resources();
        atmosphere_background_atlases_.reset();
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (use_atmosphere_environment_source() && !atmosphere_background_atlases_.has_value()) {
            atmosphere_background_atlases_.emplace(
                cubey::render::create_atmosphere_background_generated_textures(
                    device, gpu,
                    {
                        .lunar_extent = 128,
                        .night_sky_extent = 128,
                    }));
        }
        attach_project_gpu(gpu);
        resources_.create_global_resources_if_needed(device, gpu, runtime_.gpu(), pyro_config_,
                                                     frame_slot_count);
    }

    void attach_project_gpu(cubey::vulkan::GpuRuntime& gpu) {
        runtime_.attach_gpu_if_needed(gpu);
    }

    void detach_project_gpu() {
        runtime_.detach_gpu_if_attached();
    }

    void retire_project_gpu_work() {
        static_cast<void>(runtime_.retire_completed_gpu_work());
    }

    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent) {
        resources_.create_render_pipeline(device, color_format, extent,
                                          atmosphere_background_bindings());
    }

    void record_frame(cubey::host::WindowedAppContext& context,
                      const cubey::host::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        (void)context;
        cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
        if (profiler != nullptr) {
            profiler->collect(render_frame.frame_slot.index);
            maybe_print_gpu_timings(frame);
        }
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (profiler != nullptr) {
            profiler->begin_frame(render_frame.command_buffer, render_frame.frame_slot.index);
        }
        const Pyro3DConfig draw_config = render_config();
        const Pyro3DRenderCamera camera = render_camera();
        const bool atmosphere_background_enabled = atmosphere_background_bindings().has_value();
        resources_.upload_environment_lighting(render_frame.frame_slot.index,
                                               environment_lighting_uniforms());
        if (atmosphere_background_enabled) {
            resources_.upload_atmosphere_background(
                render_frame.frame_slot.index,
                atmosphere_background_uniforms(camera, render_frame.color_target.extent));
            if (moon_body_render_enabled()) {
                resources_.upload_moon_body(
                    render_frame.frame_slot.index,
                    moon_body_frame_uniforms(render_frame.color_target.extent));
            }
        }
        record_pyro_3d_compute(render_frame.command_buffer, resources_, draw_config, paused_,
                               reset_requested_, frame, source_gpu_, frame_state_, true, profiler,
                               render_frame.frame_slot.index);
        cubey::render::record_present_render_target(
            recorder, cubey::render::render_target_view(render_frame.color_target),
            [this, &render_frame, draw_config, camera, atmosphere_background_enabled,
             profiler](const cubey::vulkan::CommandRecorder& present_recorder) {
                record_pyro_3d_draw(present_recorder.handle(), resources_, draw_config, debug_view_,
                                    camera, render_frame.color_target, frame_state_, profiler,
                                    render_frame.frame_slot.index, atmosphere_background_enabled,
                                    moon_body_render_enabled());
            });
        recorder.end("vkEndCommandBuffer pyro_3d");
    }

    void maybe_print_gpu_timings(const ProjectFrame& frame) {
        if (!config_.print_frame_stats) {
            return;
        }
        const std::vector<cubey::vulkan::GpuPassTiming>& timings = resources_.latest_timings();
        if (timings.empty()) {
            return;
        }
        if (last_gpu_timing_print_seconds_ >= 0.0 &&
            frame.elapsed_seconds - last_gpu_timing_print_seconds_ < 1.0) {
            return;
        }
        last_gpu_timing_print_seconds_ = frame.elapsed_seconds;
        std::printf("pyro_3d_gpu:");
        for (const cubey::vulkan::GpuPassTiming& timing : timings) {
            std::printf(" %s=%.3fms", timing.label.c_str(), timing.milliseconds);
        }
        std::printf("\n");
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          const ProjectFrame& frame) {
        update_atmosphere_time(frame.delta_seconds);
        update_sources(frame);
        static_cast<void>(gpu.submit_and_wait({
            .label = "pyro_3d headless simulation frame",
            .work =
                [this, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    const Pyro3DConfig draw_config = render_config();
                    resources_.upload_environment_lighting(0, environment_lighting_uniforms());
                    record_pyro_3d_compute(commands.command_buffer(), resources_, draw_config,
                                           paused_, reset_requested_, frame, source_gpu_,
                                           frame_state_);
                    commands.submit_and_wait();
                },
        }));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_global_resources_if_needed(context.device(), context.gpu(), 1);
            create_render_pipeline(context.device(), target.format, target.extent);
        };
        if (config_.capture_mode == CaptureMode::Video) {
            orbit_controller_.set_auto_rotation_speed(kHeadlessVideoOrbitSpeed);
            callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                            const cubey::host::HeadlessCaptureFrame& frame) {
                orbit_controller_.update(frame.timing.delta_seconds);
            };
        }
        cubey::host::install_headless_simulation_driver(
            callbacks, config_,
            {
                .png_frame_count = pyro_3d_headless_frame_count(config_),
                .png_timing =
                    [this](std::uint64_t simulation_frame) {
                        return fixed_pyro_3d_headless_timing(pyro_config_, simulation_frame);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext&,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
                        record_headless_simulation_frame(runtime_.gpu(), project_frame);
                    },
            });
        callbacks.record_capture = [this](cubey::host::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::host::HeadlessRenderTarget& target) {
            const Pyro3DConfig draw_config = render_config();
            const Pyro3DRenderCamera camera = render_camera();
            const bool atmosphere_background_enabled = atmosphere_background_bindings().has_value();
            resources_.upload_environment_lighting(0, environment_lighting_uniforms());
            if (atmosphere_background_enabled) {
                resources_.upload_atmosphere_background(
                    0, atmosphere_background_uniforms(camera, target.extent));
                if (moon_body_render_enabled()) {
                    resources_.upload_moon_body(0, moon_body_frame_uniforms(target.extent));
                }
            }
            record_pyro_3d_draw(command_buffer, resources_, draw_config, debug_view_, camera,
                                target, frame_state_, nullptr, 0, atmosphere_background_enabled,
                                moon_body_render_enabled());
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_all_resources();
            retire_project_gpu_work();
            detach_project_gpu();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    RunConfig config_;
    Pyro3DAppInfo app_info_;
    cubey::ProjectRuntimeAdapter runtime_;
    Pyro3DConfig pyro_config_;
    std::vector<Pyro3DSourceState> source_states_;
    std::vector<Pyro3DSourceGpu> source_gpu_;
    cubey::AtmosphereEnvironmentRunState atmosphere_state_;
    Pyro3DGpuResources resources_;
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_background_atlases_;
    Pyro3DFrameState frame_state_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_{0.25};
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    Pyro3DDebugView debug_view_ = Pyro3DDebugView::Smoke;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    double last_gpu_timing_print_seconds_ = -1.0;
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_pyro_3d(const RunConfig& config, Pyro3DAppInfo app_info) {
    Pyro3DApp app(config, app_info);
    return app.run();
}

} // namespace cubey::projects::fluid::pyro_3d
