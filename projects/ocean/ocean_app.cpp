#include "ocean_app.h"

#include "ocean_config.h"
#include "ocean_ui.h"

#include <cubey/core/math.h>
#include <cubey/core/profiling.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/target.h>
#include <cubey/scene/camera_3d.h>
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

#ifndef CUBEY_OCEAN_SHADER_DIR
#error "CUBEY_OCEAN_SHADER_DIR must be defined by the ocean CMake target"
#endif

namespace cubey::projects::ocean {
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kCameraDistance = 220.0F;
constexpr float kCameraMinDistance = 18.0F;
constexpr float kCameraMaxDistance = 900.0F;
constexpr float kCameraBaseYaw = 0.38F;
constexpr float kCameraBasePitch = -0.46F;
constexpr float kCameraNearPlane = 0.25F;
constexpr float kCameraFarPlane = 6800.0F;

struct OceanPushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 mesh_options;
    cubey::math::Vec4 wave_options;
    cubey::math::Vec4 detail_options;
    cubey::math::Vec4 shading_options;
    cubey::math::Vec4 display_transform;
    cubey::math::Vec4 disturbance;
    cubey::math::Vec4 debug_options;
};

static_assert(sizeof(OceanPushConstants) == sizeof(float) * 48U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_OCEAN_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo ocean_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(OceanPushConstants),
    };
    return {
        .label = "ocean.surface",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = false,
        .depth_write = false,
        .blend_enable = false,
    };
}

[[nodiscard]] float radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

[[nodiscard]] std::uint32_t triangle_count(const OceanConfig& config) {
    return config.mesh_cells * config.mesh_cells * 2U;
}

class OceanApp {
  public:
    explicit OceanApp(RunConfig config)
        : config_(std::move(config)), ocean_config_(ocean_config_from_run_config(config_)) {
        camera_.set_projection(camera_.fovy_radians(), kCameraNearPlane, kCameraFarPlane);
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_distance_limits(kCameraMinDistance, kCameraMaxDistance);
        orbit_controller_.set_auto_rotation_speed(0.0F);
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
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            update_windowed(context, timing);
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) {
            draw_ocean_ui({
                .config = ocean_config_,
                .latest_frame_stats = latest_frame_stats_,
                .render_view = render_view_,
                .paused = paused_,
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
                .run_config = config_,
                .app_name = "ocean",
                .ready_status = "rendering ocean project",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
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
            time_seconds_ = frame.timing.elapsed_seconds;
            record_ocean_target(command_buffer, target, false);
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
            orbit_controller_.reset();
            reset_requested_ = false;
        }
        if (!paused_) {
            time_seconds_ += timing.delta_seconds;
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

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("ocean.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("ocean.frag.spv")),
        };
        const cubey::render::MaterialPassInfo pass = ocean_pass_info();
        pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                      .extent = extent,
                                      .color_format = color_format,
                                      .shader_stage_files = shader_stage_files,
                                      .material_pass = pass,
                                  });
        pipeline_color_format_ = color_format;
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline() const {
        if (!pipeline_.has_value()) {
            throw std::runtime_error("ocean pipeline is not initialized");
        }
        return pipeline_.value();
    }

    [[nodiscard]] cubey::Transform3D camera_transform() const {
        return cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = {0.0F, 0.0F, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = kCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = kCameraBasePitch + orbit_controller_.pitch(),
        });
    }

    [[nodiscard]] OceanPushConstants push_constants(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::render::PbrDisplayTransform display_transform =
            cubey::render::pbr_display_transform_for_target(pipeline_color_format_,
                                                            ocean_config_.exposure);
        const cubey::math::Vec4 display_uniform =
            cubey::render::pbr_display_transform_uniform(display_transform);

        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .camera_time =
                {
                    transform.translation.x,
                    transform.translation.y,
                    transform.translation.z,
                    static_cast<float>(time_seconds_),
                },
            .mesh_options =
                {
                    static_cast<float>(ocean_config_.mesh_cells),
                    ocean_config_.mesh_extent,
                    ocean_config_.mesh_snap,
                    ocean_config_.horizon_fog,
                },
            .wave_options =
                {
                    ocean_config_.wave_amplitude,
                    radians(ocean_config_.wind_direction_degrees),
                    ocean_config_.wind_speed,
                    ocean_config_.chop,
                },
            .detail_options =
                {
                    ocean_config_.swell_scale,
                    ocean_config_.normal_strength,
                    ocean_config_.foam_amount,
                    ocean_config_.foam_threshold,
                },
            .shading_options =
                {
                    ocean_config_.absorption,
                    ocean_config_.refraction_strength,
                    ocean_config_.shoreline_influence,
                    0.0F,
                },
            .display_transform = display_uniform,
            .disturbance =
                {
                    0.0F,
                    0.0F,
                    ocean_config_.disturbance_radius,
                    ocean_config_.disturbance_strength,
                },
            .debug_options =
                {
                    static_cast<float>(static_cast<std::uint32_t>(render_view_)),
                    0.0F,
                    0.0F,
                    0.0F,
                },
        };
    }

    void record_ocean_draw(const cubey::vulkan::CommandRecorder& recorder,
                           VkExtent2D extent) const {
        const OceanPushConstants constants = push_constants(extent);
        const cubey::render::GraphicsPipelineResource& ocean_pipeline = pipeline();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, ocean_pipeline.pipeline());
        recorder.push_constants(ocean_pipeline.layout(),
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                constants);
        recorder.draw(ocean_mesh_vertex_count(ocean_config_));
    }

    void record_ocean_target(VkCommandBuffer command_buffer, cubey::render::ColorTargetView target,
                             bool present) const {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        auto record_pass = [this, target](const cubey::vulkan::CommandRecorder& pass_recorder) {
            cubey::render::record_render_target_pass(
                pass_recorder, cubey::render::render_target_view(target),
                cubey::render::RenderClearValues{
                    .color = cubey::render::color_clear_value(0.008F, 0.019F, 0.035F, 1.0F),
                },
                [this, target](const cubey::vulkan::CommandRecorder& draw_recorder) {
                    record_ocean_draw(draw_recorder, target.extent);
                });
        };

        if (present) {
            cubey::render::record_present_render_target(recorder,
                                                        cubey::render::render_target_view(target),
                                                        record_pass);
            return;
        }
        record_pass(recorder);
    }

    void record_windowed_frame(const cubey::host::WindowedRenderFrame& frame) const {
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        record_ocean_target(frame.command_buffer, frame.color_target, true);
        recorder.end("vkEndCommandBuffer ocean");
    }

    RunConfig config_;
    OceanConfig ocean_config_;
    OceanRenderView render_view_ = ocean_config_.render_view;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    std::optional<cubey::render::GraphicsPipelineResource> pipeline_;
    VkFormat pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    double time_seconds_ = 0.0;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    bool paused_ = false;
    bool reset_requested_ = false;
};

} // namespace

int run_ocean(const RunConfig& config) {
    OceanApp app(config);
    return app.run();
}

} // namespace cubey::projects::ocean
