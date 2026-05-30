#include "ocean_app.h"

#include "ocean_config.h"
#include "ocean_gpu_resources.h"
#include "ocean_mesh.h"
#include "ocean_ui.h"

#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/memory_barriers.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

constexpr float kCameraDistance = 125.0F;
constexpr float kCameraMinDistance = 18.0F;
constexpr float kCameraMaxDistance = 900.0F;
constexpr float kCameraBaseYaw = 0.38F;
constexpr float kCameraBasePitch = -0.22F;
constexpr float kCameraNearPlane = 0.25F;
constexpr float kCameraFarPlane = 14000.0F;
constexpr VkFormat kOceanDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr float kGravity = 9.81F;

struct OceanPushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 mesh_options;
    cubey::math::Vec4 patch_bounds;
    cubey::math::Vec4 display_transform;
    cubey::math::Vec4 debug_options;
    cubey::math::Vec4 inspection_options;
    cubey::math::Vec4 tile_lengths;
    cubey::math::Vec4 displacement_scales;
    cubey::math::Vec4 normal_scales;
    cubey::math::Vec4 cascade4_options;
    cubey::math::Vec4 water_color;
    cubey::math::Vec4 foam_color;
};

struct OceanSkyPushConstants {
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward;
    cubey::math::Vec4 display_transform;
};

struct OceanSpectrumPushConstants {
    cubey::math::Vec4 seed_tile;
    cubey::math::Vec4 spectrum_options;
    cubey::math::Vec4 shape_options;
    cubey::math::Vec4 cascade_options;
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
static_assert(sizeof(OceanSkyPushConstants) == sizeof(float) * 20U);
static_assert(sizeof(OceanSpectrumPushConstants) == sizeof(float) * 16U);
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
        return {.preset = preset, .distance = kCameraDistance, .yaw = kCameraBaseYaw,
                .pitch = kCameraBasePitch};
    case OceanCameraPreset::Low:
        return {.preset = preset, .distance = 180.0F, .yaw = 0.42F, .pitch = -0.08F};
    case OceanCameraPreset::Close:
        return {.preset = preset, .distance = 48.0F, .yaw = 0.62F, .pitch = -0.28F};
    case OceanCameraPreset::Overhead:
        return {.preset = preset, .distance = 220.0F, .yaw = 0.20F, .pitch = -1.05F};
    }
    return {.preset = OceanCameraPreset::Default, .distance = kCameraDistance,
            .yaw = kCameraBaseYaw, .pitch = kCameraBasePitch};
}

[[nodiscard]] bool ocean_resolution_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.map_size != rhs.map_size;
}

class OceanApp {
  public:
    explicit OceanApp(RunConfig config)
        : config_(std::move(config)), ocean_config_(ocean_config_from_run_config(config_)),
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
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_windowed(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) {
            draw_ocean_ui({
                .config = ocean_config_,
                .diagnostics = diagnostics_,
                .latest_frame_stats = latest_frame_stats_,
                .render_view = render_view_,
                .camera_preset = camera_preset_,
                .paused = paused_,
                .reset_requested = reset_requested_,
                .step_requested = step_requested_,
                .camera_preset_requested = camera_preset_requested_,
                .latest_fps = latest_fps_,
                .latest_frame_ms = latest_frame_ms_,
            });
            if (camera_preset_requested_) {
                apply_camera_preset(camera_preset_);
                camera_preset_requested_ = false;
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
            create_pipeline(context.device(), target.format, target.extent,
                            cubey::host::headless_capture_frame_slot_count(config_));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            time_seconds_ = frame.timing.elapsed_seconds;
            last_delta_seconds_ =
                frame.timing.delta_seconds > 0.0 ? frame.timing.delta_seconds : (1.0 / 60.0);
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
        sync_gpu_resources(context);
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

    void create_pipeline(cubey::vulkan::Device& device, VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count = 1U) {
        ocean_gpu_.create(device, OceanGpuResourceConfig{
                                      .ocean = ocean_config_,
                                      .shader_dir = CUBEY_OCEAN_SHADER_DIR,
                                      .color_format = color_format,
                                      .depth_format = kOceanDepthFormat,
                                      .target_extent = extent,
                                  });
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
        pipeline_color_format_ = color_format;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        gpu_config_ = ocean_config_;
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        ocean_gpu_.reset();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        gpu_config_.reset();
    }

    void sync_gpu_resources(cubey::host::WindowedAppContext& context) {
        validate_ocean_config(ocean_config_);
        if (!gpu_config_.has_value()) {
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
            return;
        }
        if (ocean_resolution_changed(ocean_config_, gpu_config_.value())) {
            cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                                 "vkDeviceWaitIdle before ocean resource recreation");
            create_pipeline(context.device(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
            return;
        }
        if (ocean_config_ != gpu_config_.value()) {
            spectrum_initialized_ = false;
            foam_initialized_ = false;
            gpu_config_ = ocean_config_;
        }
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

    [[nodiscard]] OceanPushConstants
    surface_push_constants(VkExtent2D extent, const OceanMeshPatch& patch) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::math::Mat4 view_projection = camera_.view_projection_matrix(transform, aspect);
        const cubey::render::PbrDisplayTransform display_transform =
            cubey::render::pbr_display_transform_for_target(pipeline_color_format_,
                                                            ocean_config_.exposure);
        const cubey::math::Vec4 display_uniform =
            cubey::render::pbr_display_transform_uniform(display_transform);

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
            .display_transform = display_uniform,
            .debug_options =
                {
                    static_cast<float>(static_cast<std::uint32_t>(render_view_)),
                    static_cast<float>(patch.level),
                    static_cast<float>(ocean_config_.mesh_lod_levels - 1U),
                    diagnostics_.wire_overlay ? std::clamp(diagnostics_.wire_opacity, 0.0F, 1.0F)
                                              : 0.0F,
                },
            .inspection_options =
                {
                    static_cast<float>(diagnostics_.selected_cascade),
                    static_cast<float>(ocean_config_.map_size),
                    ocean_config_.normal_strength,
                    ocean_config_.cascades[4].tile_length,
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
                    ocean_config_.cascades[4].displacement_scale,
                    ocean_config_.cascades[4].normal_scale,
                    0.0F,
                    0.0F,
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
                    0.0F,
                },
        };
    }

    [[nodiscard]] OceanSkyPushConstants sky_push_constants(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float tan_half_fovy = std::tan(camera_.fovy_radians() * 0.5F);
        const cubey::math::Vec3 right =
            glm::normalize(transform.rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F});
        const cubey::math::Vec3 up =
            glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F});
        const cubey::math::Vec3 forward =
            glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});
        const cubey::render::PbrDisplayTransform display_transform =
            cubey::render::pbr_display_transform_for_target(pipeline_color_format_,
                                                            ocean_config_.exposure);
        const cubey::math::Vec4 display_uniform =
            cubey::render::pbr_display_transform_uniform(display_transform);

        return {
            .camera_time =
                {
                    transform.translation.x,
                    transform.translation.y,
                    transform.translation.z,
                    static_cast<float>(time_seconds_),
                },
            .camera_right_aspect = {right.x, right.y, right.z, aspect},
            .camera_up_tan_half_fovy = {up.x, up.y, up.z, tan_half_fovy},
            .camera_forward = {forward.x, forward.y, forward.z, 0.0F},
            .display_transform = display_uniform,
        };
    }

    [[nodiscard]] OceanSpectrumPushConstants
    spectrum_push_constants(std::uint32_t cascade_index) const {
        const OceanCascadeConfig& cascade = ocean_cascade(ocean_config_, cascade_index);
        const OceanSeaStateConfig& sea_state = ocean_config_.sea_state;
        const float fetch_m = sea_state.fetch_length_km * 1000.0F;
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
                    jonswap_alpha(sea_state.wind_speed, fetch_m),
                    jonswap_peak_frequency(sea_state.wind_speed, fetch_m),
                    sea_state.wind_speed,
                    radians(sea_state.wind_direction_degrees),
                },
            .shape_options =
                {
                    ocean_config_.depth,
                    sea_state.swell,
                    sea_state.detail,
                    sea_state.spread,
                },
            .cascade_options =
                {
                    static_cast<float>(cascade_index),
                    static_cast<float>(ocean_config_.map_size),
                    cascade.min_wavelength,
                    cascade.max_wavelength,
                },
        };
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

    void record_ocean_sky(const cubey::vulkan::CommandRecorder& recorder,
                              VkExtent2D extent) const {
        const OceanSkyPushConstants constants = sky_push_constants(extent);
        const cubey::render::GraphicsPipelineResource& sky_pipeline = ocean_gpu_.sky_pipeline();
        cubey::render::record_fullscreen_pipeline_draw(
            recorder,
            {
                .pipeline = &sky_pipeline,
            },
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, constants);
    }

    void record_ocean_draw(const cubey::vulkan::CommandRecorder& recorder,
                               VkExtent2D extent) const {
        const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(ocean_config_);
        const cubey::render::GraphicsPipelineResource& surface_pipeline =
            ocean_gpu_.surface_pipeline();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pipeline.pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pipeline.layout(), 0,
                                     ocean_gpu_.surface_set());
        for (const OceanMeshPatch& patch : patches) {
            const OceanPushConstants constants = surface_push_constants(extent, patch);
            recorder.push_constants(surface_pipeline.layout(),
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    constants);
            recorder.draw(ocean_mesh_patch_vertex_count(patch));
        }
    }

    [[nodiscard]] OceanFrameGraph
    build_ocean_frame_graph(cubey::render::ColorTargetView color_target,
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
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "ocean backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle surface_depth =
            graph.create_texture(ocean_depth_texture_desc(
                "ocean surface depth", color_target.extent, kOceanDepthFormat));

        graph.add_pass("ocean final", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(backbuffer)
            .write_depth(surface_depth)
            .execute([this, backbuffer,
                      surface_depth](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, backbuffer);
                const cubey::render::DepthTargetView depth =
                    cubey::render::resolved_depth_target_view(context, surface_depth);
                cubey::render::record_render_target_pass(
                    context.recorder(), cubey::render::render_target_view(target, depth),
                    cubey::render::RenderClearValues{
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
                    [this, target](const cubey::vulkan::CommandRecorder& draw_recorder) {
                        record_ocean_sky(draw_recorder, target.extent);
                        record_ocean_draw(draw_recorder, target.extent);
                    });
            });

        return {
            .graph = graph.compile(),
            .backbuffer = backbuffer,
            .surface_depth = surface_depth,
        };
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
                ocean_gpu_.normal_foam(cascade).handle()));
        }
    }

    void record_spectrum_init(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.spectrum_pipeline(), ocean_gpu_.spectrum_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, spectrum_push_constants(cascade));
        }
    }

    void record_modulate(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
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
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_);
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
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
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

    void record_ocean_target(VkCommandBuffer command_buffer,
                                 const cubey::vulkan::Device& device,
                                 cubey::render::ColorTargetView target,
                                 cubey::render::FrameSlot frame_slot,
                                 OceanRenderTargetMode target_mode) {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        record_ocean_compute(recorder);
        const OceanFrameGraph frame_graph = build_ocean_frame_graph(target, target_mode);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer ocean graph",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            frame_graph.graph);
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
    OceanDiagnosticsConfig diagnostics_;
    OceanRenderView render_view_ = OceanRenderView::Final;
    OceanCameraPreset camera_preset_ = OceanCameraPreset::Default;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    OceanGpuResources ocean_gpu_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
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
