#include "water_3d_app.h"

#include "water_3d_commands.h"
#include "water_3d_config.h"
#include "water_3d_diagnostics.h"
#include "water_3d_gpu_resources.h"
#include "water_3d_ui.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/core/profiling.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/environment_lighting.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::water_3d {
namespace {

using cubey::FrameTiming;
using cubey::ProjectFrame;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kCameraDistance = 4.0F;
constexpr float kCameraNearPlane = 0.005F;
constexpr float kCameraFarPlane = 20.0F;
constexpr float kCameraMinDistance = 0.55F;
constexpr float kCameraBaseYaw = -0.45F;
constexpr float kCameraBasePitch = -0.34F;
constexpr float kHeadlessVideoOrbitSpeed = 0.32F;
constexpr cubey::math::Vec3 kVolumeCenter{0.5F, 0.5F, 0.5F};

[[nodiscard]] bool use_atmosphere_environment_source(const RunConfig& config) {
    return config.pbr.environment_source.empty() || config.pbr.environment_source == "atmosphere";
}

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
water_3d_atmosphere_run_state(const RunConfig& config) {
    return cubey::atmosphere_environment_run_state_from_config(
        config.atmosphere,
        {
            .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnly,
            .reference_geometry_enabled = false,
        });
}

[[nodiscard]] cubey::render::EnvironmentLightingUniforms
water_3d_static_environment_lighting(float exposure, float intensity) {
    cubey::render::AtmosphereEnvironmentLighting lighting;
    lighting.primary_light_direction = glm::normalize(cubey::math::Vec3{-0.28F, 0.80F, 0.52F});
    lighting.primary_light_color = cubey::math::Vec3{1.0F, 1.0F, 1.0F};
    lighting.primary_light_intensity = 1.0F;
    lighting.ambient_color = cubey::math::Vec3{0.42F, 0.42F, 0.42F};
    lighting.ambient_intensity = 1.0F;
    lighting.diffuse_irradiance_sh[0] = cubey::math::Vec3{0.42F, 0.42F, 0.42F};
    return cubey::render::environment_lighting_uniforms(lighting, exposure, intensity);
}

[[nodiscard]] std::filesystem::path bundled_sample_environment_path() {
#ifdef CUBEY_HDR_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_HDR_SAMPLE_ASSETS_DIR) / "lightroom_14b.hdr";
#else
    return {};
#endif
}

class Water3DApp {
  public:
    Water3DApp(RunConfig config, Water3DAppInfo app_info)
        : config_(std::move(config)), app_info_(app_info), runtime_(1),
          water_config_(water_3d_config_from_run_config(config_)),
          atmosphere_state_(water_3d_atmosphere_run_state(config_)),
          render_view_(water_3d_render_view_from_name(config_.debug_view)) {
        if (use_atmosphere_environment_source()) {
            atmosphere_runtime_.set_environment(atmosphere_state_.environment);
        }
        camera_.set_projection(camera_.fovy_radians(), kCameraNearPlane, kCameraFarPlane);
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_distance_limits(kCameraMinDistance, 80.0F);
        orbit_controller_.set_auto_rotation_speed(0.0F);
    }

    Water3DApp(const Water3DApp&) = delete;
    Water3DApp& operator=(const Water3DApp&) = delete;

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
            create_render_pipeline(context.device(), context.swapchain().format(),
                                   context.swapchain().extent());
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
            return record_frame_stats(context, timing);
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
            render_view_ = next_render_view(render_view_);
        }

        update_camera_input(context, project_frame.delta_seconds);
        update_atmosphere_time(project_frame.delta_seconds);
    }

    void update_camera_input(cubey::host::WindowedAppContext& context, double delta_seconds) {
        orbit_controller_.update_pointer_input(context.filtered_input(), delta_seconds);
    }

    std::optional<FrameStatsSample> record_frame_stats(cubey::host::WindowedAppContext& context,
                                                       const FrameTiming& timing) {
        const VkExtent2D extent = context.swapchain().extent();
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const std::uint32_t particle_scan_count =
            water_3d_runtime_particle_scan_count(water_config_, runtime_state_);
        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles =
                render_view_ == Water3DRenderView::Particles
                    ? particle_scan_count * 2U
                    : (is_water_3d_surface_view(render_view_) ? particle_scan_count * 4U + 8U : 2U),
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        if (draw_water_3d_ui({
                .title = app_info_.ui_title,
                .config = water_config_,
                .atmosphere =
                    use_atmosphere_environment_source() ? &atmosphere_state_ : nullptr,
                .runtime_state = runtime_state_,
                .resources = resources_,
                .device = context.device(),
                .latest_frame_stats = latest_frame_stats_,
                .render_view = render_view_,
                .paused = paused_,
                .reset_requested = reset_requested_,
                .latest_fps = latest_fps_,
                .latest_frame_ms = latest_frame_ms_,
            })) {
            refresh_atmosphere_environment();
        }
    }

    void reset_simulation() {
        reset_requested_ = true;
        runtime_state_ = {};
    }

    [[nodiscard]] bool use_atmosphere_environment_source() const {
        return cubey::projects::fluid::water_3d::use_atmosphere_environment_source(config_);
    }

    void refresh_atmosphere_environment() {
        cubey::atmosphere_environment_resolve_run_state(atmosphere_state_);
        if (atmosphere_runtime_.resources_created()) {
            atmosphere_runtime_.set_environment(atmosphere_state_.environment);
            atmosphere_runtime_.mark_full_update_pending();
        }
    }

    void update_atmosphere_time(double delta_seconds) {
        if (!use_atmosphere_environment_source()) {
            return;
        }
        if (cubey::atmosphere_environment_advance_time(atmosphere_state_, delta_seconds)) {
            refresh_atmosphere_environment();
        }
    }

    [[nodiscard]] float resolved_exposure() const {
        if (use_atmosphere_environment_source() && atmosphere_state_.auto_exposure_enabled &&
            !config_.pbr.exposure_explicit) {
            return atmosphere_state_.resolved_exposure;
        }
        return water_config_.exposure;
    }

    [[nodiscard]] Water3DConfig render_config() const {
        Water3DConfig result = water_config_;
        result.exposure = resolved_exposure();
        return result;
    }

    [[nodiscard]] cubey::render::EnvironmentLightingUniforms environment_lighting_uniforms() const {
        if (!use_atmosphere_environment_source()) {
            return water_3d_static_environment_lighting(resolved_exposure(),
                                                       water_config_.environment_intensity);
        }
        return cubey::render::environment_lighting_uniforms(
            atmosphere_runtime_.lighting(), resolved_exposure(), water_config_.environment_intensity);
    }

    [[nodiscard]] Water3DEnvironmentTextureBindings water_environment_bindings() const {
        cubey::render::PbrEnvironmentTextureBindings pbr =
            use_atmosphere_environment_source()
                ? atmosphere_runtime_.pbr_environment_bindings(ibl_environment())
                : cubey::render::pbr_environment_texture_bindings(ibl_environment());
        if (!use_atmosphere_environment_source()) {
            return {
                .pbr = pbr,
                .display_sampler = pbr.prefiltered_sampler,
                .display_view = pbr.prefiltered_view,
                .display_layout = pbr.prefiltered_layout,
            };
        }
        return {
            .pbr = pbr,
            .display_sampler = pbr.prefiltered_sampler,
            .display_view = pbr.prefiltered_view,
            .display_layout = pbr.prefiltered_layout,
            .atmosphere_background_textures = atmosphere_background_atlases_->bindings(),
        };
    }

    [[nodiscard]] Water3DRenderCamera render_camera(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::Transform3D transform = cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = kVolumeCenter,
            .distance = orbit_controller_.distance(),
            .yaw = kCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = kCameraBasePitch + orbit_controller_.pitch(),
        });
        const cubey::math::Quat rotation = transform.rotation;
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .position = transform.translation,
            .right = rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F},
            .up = rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F},
            .forward = rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F},
            .fovy_radians = camera_.fovy_radians(),
        };
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_background_uniforms(const Water3DRenderCamera& camera, VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::render::ViewRayBasis3D view_rays{
            .right_aspect = {camera.right.x, camera.right.y, camera.right.z, aspect},
            .up_tan_half_fovy = {camera.up.x, camera.up.y, camera.up.z,
                                 std::tan(camera.fovy_radians * 0.5F)},
            .forward = {camera.forward.x, camera.forward.y, camera.forward.z, 0.0F},
        };
        return atmosphere_runtime_
            .frame({
                .view_rays = view_rays,
                .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
            })
            .background;
    }

    void destroy_swapchain_resources() {
        resources_.destroy_swapchain_resources();
    }

    void destroy_all_resources() {
        surface_graph_executor_.clear();
        resources_.destroy_all_resources();
        atmosphere_runtime_.destroy();
        atmosphere_background_atlases_.reset();
        ibl_environment_.reset();
    }

    [[nodiscard]] std::filesystem::path resolved_environment_path() const {
        if (!config_.pbr.environment_path.empty()) {
            if (!std::filesystem::exists(config_.pbr.environment_path)) {
                throw std::runtime_error("environment HDR does not exist: " +
                                         config_.pbr.environment_path.string());
            }
            return config_.pbr.environment_path;
        }

        const std::filesystem::path sample = bundled_sample_environment_path();
        if (!sample.empty() && std::filesystem::exists(sample)) {
            return sample;
        }
        return {};
    }

    void create_environment_resources_if_needed(cubey::vulkan::Device& device,
                                                cubey::vulkan::GpuRuntime& gpu) {
        if (ibl_environment_.has_value()) {
            return;
        }

        cubey::render::GeneratedPbrEnvironmentConfig ibl_config;
        ibl_config.intensity = 1.0F;
        const std::filesystem::path environment = resolved_environment_path();
        if (!environment.empty()) {
            const cubey::asset::HdrImage image = cubey::asset::load_hdr_image(environment);
            ibl_environment_.emplace(cubey::render::create_pbr_environment_from_equirectangular(
                device, gpu,
                cubey::render::PbrEquirectangularImage{
                    .width = image.width,
                    .height = image.height,
                    .rgba32f = image.rgba32f,
                },
                ibl_config));
            return;
        }

        ibl_environment_.emplace(
            cubey::render::create_generated_pbr_environment(device, gpu, ibl_config));
    }

    [[nodiscard]] const cubey::render::GeneratedPbrEnvironment& ibl_environment() const {
        if (!ibl_environment_.has_value()) {
            throw std::runtime_error("water 3D IBL environment is not initialized");
        }
        return ibl_environment_.value();
    }

    void create_atmosphere_environment_runtime(cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu,
                                               std::uint32_t frame_slot_count) {
        if (!use_atmosphere_environment_source()) {
            return;
        }
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
            const std::filesystem::path shader_dir = CUBEY_WATER_3D_SHADER_DIR;
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

    void create_global_resources_if_needed(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        create_environment_resources_if_needed(device, gpu);
        create_atmosphere_environment_runtime(device, gpu, frame_slot_count);
        attach_project_gpu(gpu);
        resources_.create_global_resources_if_needed(device, runtime_.gpu(), water_config_,
                                                     frame_slot_count);
        surface_graph_executor_.resize(frame_slot_count);
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
                                          water_environment_bindings());
    }

    void record_frame(cubey::host::WindowedAppContext& context,
                      const cubey::host::WindowedRenderFrame& render_frame,
                      const ProjectFrame& frame) {
        cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
        if (profiler != nullptr) {
            profiler->collect(render_frame.frame_slot.index);
            record_gpu_timings(context.profile_recorder(),
                               collected_profile_frame_index(frame, render_frame.frame_slot),
                               resources_.latest_timings());
            maybe_print_gpu_timings(frame);
        }
        const cubey::vulkan::CommandRecorder recorder(render_frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        if (profiler != nullptr) {
            profiler->begin_frame(render_frame.command_buffer, render_frame.frame_slot.index);
        }
        record_water_3d_compute(render_frame.command_buffer, resources_, water_config_,
                                runtime_state_, render_frame.frame_slot, paused_, reset_requested_,
                                frame, true, profiler);
        if (is_water_3d_surface_view(render_view_)) {
            const Water3DConfig draw_config = render_config();
            const Water3DRenderCamera camera = render_camera(render_frame.color_target.extent);
            resources_.upload_environment_lighting(render_frame.frame_slot,
                                                   environment_lighting_uniforms());
            if (use_atmosphere_environment_source()) {
                resources_.upload_atmosphere_background(
                    render_frame.frame_slot,
                    atmosphere_background_uniforms(camera, render_frame.color_target.extent));
                atmosphere_runtime_.record_pending_update(recorder, render_frame.frame_slot);
            }
            const Water3DEnvironmentTextureBindings environment = water_environment_bindings();
            record_water_3d_surface_draw(
                render_frame.command_buffer, context.device(), surface_graph_executor_, resources_,
                draw_config, render_frame.frame_slot, runtime_state_, render_view_,
                camera, render_frame.color_target, Water3DRenderTargetMode::Present, environment,
                profiler);
        } else {
            cubey::render::record_present_render_target(
                recorder, cubey::render::render_target_view(render_frame.color_target),
                [this, &render_frame,
                 profiler](const cubey::vulkan::CommandRecorder& present_recorder) {
                    record_water_3d_draw(present_recorder.handle(), resources_, water_config_,
                                         render_frame.frame_slot, runtime_state_, render_view_,
                                         render_camera(render_frame.color_target.extent),
                                         render_frame.color_target, profiler);
                });
        }
        recorder.end("vkEndCommandBuffer water_3d");
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
        std::printf("water_3d_gpu:");
        for (const cubey::vulkan::GpuPassTiming& timing : timings) {
            std::printf(" %s=%.3fms", timing.label.c_str(), timing.milliseconds);
        }
        std::printf("\n");
    }

    void record_headless_simulation_frame(cubey::ProjectGpuServices& gpu,
                                          cubey::render::FrameSlot frame_slot,
                                          const ProjectFrame& frame,
                                          cubey::profiling::ProfileRecorder* profile_recorder) {
        update_atmosphere_time(frame.delta_seconds);
        const std::uint64_t frame_index = profile_frame_index(frame);
        static_cast<void>(gpu.submit_and_wait({
            .label = "water_3d headless simulation frame",
            .work =
                [this, frame_slot, frame, profile_recorder,
                 frame_index](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::ImmediateCommands commands(gpu_context);
                    cubey::vulkan::GpuTimestampProfiler* profiler = resources_.profiler();
                    if (profiler != nullptr) {
                        profiler->begin_frame(commands.command_buffer(), frame_slot.index);
                    }
                    record_water_3d_compute(commands.command_buffer(), resources_, water_config_,
                                            runtime_state_, frame_slot, paused_, reset_requested_,
                                            frame, true, profiler);
                    commands.submit_and_wait();
                    if (profiler != nullptr) {
                        profiler->collect(frame_slot.index);
                        record_gpu_timings(profile_recorder, frame_index,
                                           resources_.latest_timings());
                    }
                },
        }));
        if (should_record_water_3d_diagnostics(profile_recorder, water_config_, frame_index)) {
            const std::vector<std::uint8_t> diagnostics = gpu.readback_buffer(
                resources_.diagnostics().handle(), resources_.diagnostics().size(),
                "water_3d diagnostics readback");
            record_water_3d_diagnostics(*profile_recorder, frame_index, water_config_, diagnostics);
        }
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_global_resources_if_needed(
                context.device(), context.gpu(),
                cubey::host::headless_capture_frame_slot_count(config_));
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
                .png_frame_count = water_3d_headless_frame_count(config_),
                .png_timing =
                    [this](std::uint64_t simulation_frame) {
                        return fixed_water_3d_headless_timing(water_config_, simulation_frame);
                    },
                .simulate_frame =
                    [this](cubey::host::HeadlessPngContext& context,
                           const cubey::host::HeadlessCaptureFrame& frame) {
                        const ProjectFrame project_frame = runtime_.frame_for_timing(frame.timing);
                        record_headless_simulation_frame(runtime_.gpu(), frame.frame_slot,
                                                         project_frame, context.profile_recorder());
                    },
            });
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            if (is_water_3d_surface_view(render_view_)) {
                const cubey::vulkan::CommandRecorder recorder(command_buffer);
                const Water3DConfig draw_config = render_config();
                const Water3DRenderCamera camera = render_camera(target.extent);
                resources_.upload_environment_lighting(frame.frame_slot,
                                                       environment_lighting_uniforms());
                if (use_atmosphere_environment_source()) {
                    resources_.upload_atmosphere_background(
                        frame.frame_slot, atmosphere_background_uniforms(camera, target.extent));
                    atmosphere_runtime_.record_pending_update(recorder, frame.frame_slot);
                }
                const Water3DEnvironmentTextureBindings environment = water_environment_bindings();
                record_water_3d_surface_draw(
                    command_buffer, context.device(), surface_graph_executor_, resources_,
                    draw_config, frame.frame_slot, runtime_state_, render_view_,
                    camera, target, Water3DRenderTargetMode::ColorAttachment, environment);
            } else {
                record_water_3d_draw(command_buffer, resources_, water_config_, frame.frame_slot,
                                     runtime_state_, render_view_, render_camera(target.extent),
                                     target);
            }
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
    Water3DAppInfo app_info_;
    cubey::ProjectRuntimeAdapter runtime_;
    Water3DConfig water_config_;
    cubey::AtmosphereEnvironmentRunState atmosphere_state_;
    Water3DRuntimeState runtime_state_;
    Water3DGpuResources resources_;
    cubey::render::RenderGraphFrameExecutor surface_graph_executor_;
    std::optional<cubey::render::GeneratedPbrEnvironment> ibl_environment_;
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_background_atlases_;
    cubey::AtmosphereEnvironmentRuntime atmosphere_runtime_;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_{0.25};
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    Water3DRenderView render_view_ = Water3DRenderView::Surface;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    double last_gpu_timing_print_seconds_ = -1.0;
    bool paused_ = false;
    bool reset_requested_ = true;
};

} // namespace

int run_water_3d(const RunConfig& config, Water3DAppInfo app_info) {
    Water3DApp app(config, app_info);
    return app.run();
}

} // namespace cubey::projects::fluid::water_3d
