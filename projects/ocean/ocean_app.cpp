#include "ocean_app.h"

#include "ocean_config.h"
#include "ocean_gpu_resources.h"
#include "ocean_mesh.h"
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
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/memory_barriers.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
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
constexpr VkFormat kOceanSceneDepthFormat = VK_FORMAT_D32_SFLOAT;

struct OceanPushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 mesh_options;
    cubey::math::Vec4 patch_bounds;
    cubey::math::Vec4 wave_options;
    cubey::math::Vec4 detail_options;
    cubey::math::Vec4 shading_options;
    cubey::math::Vec4 display_transform;
    cubey::math::Vec4 disturbance;
    cubey::math::Vec4 debug_options;
    cubey::math::Vec4 spectrum_options;
    cubey::math::Vec4 cascade_options;
    cubey::math::Vec4 detail_wave_options;
};

static_assert(sizeof(OceanPushConstants) == sizeof(float) * 64U);

struct OceanSkyPushConstants {
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward;
    cubey::math::Vec4 display_transform;
};

static_assert(sizeof(OceanSkyPushConstants) == sizeof(float) * 20U);

struct OceanScenePushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 camera_right_aspect;
    cubey::math::Vec4 camera_up_tan_half_fovy;
    cubey::math::Vec4 camera_forward;
    cubey::math::Vec4 scene_options;
};

static_assert(sizeof(OceanScenePushConstants) == sizeof(float) * 36U);

struct OceanSpectrumPushConstants {
    cubey::math::Vec4 ocean_options;
    cubey::math::Vec4 cascade_options;
    cubey::math::Vec4 wind_options;
    cubey::math::Vec4 seed_options;
};

struct OceanFftPushConstants {
    cubey::math::Vec4 fft_options;
    cubey::math::Vec4 pass_options;
};

struct OceanFinalizePushConstants {
    cubey::math::Vec4 ocean_options;
    cubey::math::Vec4 cascade_options;
    cubey::math::Vec4 foam_options;
    cubey::math::Vec4 debug_options;
};

struct OceanFoamUpdatePushConstants {
    cubey::math::Vec4 foam_options;
    cubey::math::Vec4 cascade_options;
};

struct OceanDetailPushConstants {
    cubey::math::Vec4 ocean_options;
    cubey::math::Vec4 cascade_options;
    cubey::math::Vec4 wind_options;
    cubey::math::Vec4 foam_options;
};

static_assert(sizeof(OceanSpectrumPushConstants) == sizeof(float) * 16U);
static_assert(sizeof(OceanFftPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanFinalizePushConstants) == sizeof(float) * 16U);
static_assert(sizeof(OceanFoamUpdatePushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanDetailPushConstants) == sizeof(float) * 16U);

enum class OceanRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

struct OceanFrameGraph {
    cubey::render::CompiledRenderGraph graph;
    cubey::render::RenderGraphTextureHandle backbuffer{};
    cubey::render::RenderGraphTextureHandle scene_color{};
    cubey::render::RenderGraphTextureHandle scene_depth{};
};

[[nodiscard]] float radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

[[nodiscard]] std::uint32_t triangle_count(const OceanConfig& config) {
    return ocean_mesh_total_triangle_count(config);
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
ocean_color_texture_desc(const char* label, VkExtent2D extent, VkFormat format) {
    return {
        .label = label,
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
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

[[nodiscard]] cubey::render::ComputeDispatchGroups
ocean_spectrum_dispatch_groups(const OceanConfig& config) {
    return cubey::render::ceil_dispatch_groups(config.spectrum_resolution,
                                               config.spectrum_resolution, 16U);
}

[[nodiscard]] bool ocean_resolution_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.spectrum_resolution != rhs.spectrum_resolution;
}

[[nodiscard]] bool ocean_spectrum_generation_changed(const OceanConfig& lhs,
                                                     const OceanConfig& rhs) {
    return lhs.spectrum_resolution != rhs.spectrum_resolution ||
           lhs.spectrum_patch_length_near != rhs.spectrum_patch_length_near ||
           lhs.spectrum_patch_length_mid != rhs.spectrum_patch_length_mid ||
           lhs.spectrum_patch_length_far != rhs.spectrum_patch_length_far ||
           lhs.wind_direction_degrees != rhs.wind_direction_degrees ||
           lhs.sea_state != rhs.sea_state || lhs.wave_amplitude != rhs.wave_amplitude ||
           lhs.swell_scale != rhs.swell_scale || lhs.spectrum_energy != rhs.spectrum_energy ||
           lhs.spectrum_fetch != rhs.spectrum_fetch || lhs.spectrum_seed != rhs.spectrum_seed;
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
                .latest_frame_stats = latest_frame_stats_,
                .render_view = render_view_,
                .paused = paused_,
                .reset_requested = reset_requested_,
                .latest_fps = latest_fps_,
                .latest_frame_ms = latest_frame_ms_,
            });
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
            orbit_controller_.reset();
            reset_requested_ = false;
        }
        if (!paused_) {
            time_seconds_ += timing.delta_seconds;
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
                         std::uint32_t frame_slot_count) {
        ocean_gpu_.create(device, OceanGpuResourceConfig{
                                      .ocean = ocean_config_,
                                      .shader_dir = CUBEY_OCEAN_SHADER_DIR,
                                      .color_format = color_format,
                                      .scene_depth_format = kOceanSceneDepthFormat,
                                      .target_extent = extent,
                                      .frame_slot_count = frame_slot_count,
                                  });
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
        pipeline_color_format_ = color_format;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        foam_history_index_ = 0;
        gpu_config_ = ocean_config_;
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        ocean_gpu_.reset();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        foam_history_index_ = 0;
        gpu_config_.reset();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& pipeline() const {
        if (!ocean_gpu_.initialized()) {
            throw std::runtime_error("ocean pipeline is not initialized");
        }
        return ocean_gpu_.surface_pipeline();
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
        if (ocean_spectrum_generation_changed(ocean_config_, gpu_config_.value())) {
            spectrum_initialized_ = false;
            foam_initialized_ = false;
            foam_history_index_ = 0;
            gpu_config_ = ocean_config_;
        }
    }

    [[nodiscard]] cubey::Transform3D camera_transform() const {
        return cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = {0.0F, 0.0F, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = kCameraBaseYaw + orbit_controller_.yaw(),
            .pitch = kCameraBasePitch + orbit_controller_.pitch(),
        });
    }

    [[nodiscard]] OceanPushConstants push_constants(VkExtent2D extent,
                                                    const OceanMeshPatch& patch) const {
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
            .wave_options =
                {
                    ocean_config_.wave_amplitude,
                    radians(ocean_config_.wind_direction_degrees),
                    ocean_config_.animation_speed,
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
                    ocean_config_.refraction_pixels,
                    ocean_config_.shoreline_influence,
                    ocean_config_.scattering_strength,
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
                    static_cast<float>(foam_history_index_),
                    static_cast<float>(patch.level),
                    static_cast<float>(ocean_config_.mesh_lod_levels - 1U),
                },
            .spectrum_options =
                {
                    static_cast<float>(ocean_config_.spectrum_resolution),
                    ocean_config_.spectrum_energy,
                    ocean_config_.spectrum_fetch,
                    ocean_config_.water_opacity,
                },
            .cascade_options =
                {
                    ocean_config_.spectrum_patch_length_near,
                    ocean_config_.spectrum_patch_length_mid,
                    ocean_config_.spectrum_patch_length_far,
                    ocean_config_.seafloor_depth,
                },
            .detail_wave_options =
                {
                    ocean_config_.detail_chop,
                    ocean_config_.detail_spread,
                    ocean_config_.foam_breakup,
                    ocean_config_.detail_geometry,
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

    [[nodiscard]] OceanScenePushConstants scene_push_constants(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const float tan_half_fovy = std::tan(camera_.fovy_radians() * 0.5F);
        const cubey::math::Vec3 right =
            glm::normalize(transform.rotation * cubey::math::Vec3{1.0F, 0.0F, 0.0F});
        const cubey::math::Vec3 up =
            glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 1.0F, 0.0F});
        const cubey::math::Vec3 forward =
            glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F});

        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
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
            .scene_options =
                {
                    ocean_config_.seafloor_depth,
                    ocean_config_.seafloor_brightness,
                    ocean_config_.horizon_fog,
                    ocean_config_.seafloor_enabled ? ocean_config_.seafloor_detail : -1.0F,
                },
        };
    }

    void record_ocean_draw(const cubey::vulkan::CommandRecorder& recorder, VkExtent2D extent,
                           VkDescriptorSet scene_set) const {
        const OceanMeshPatchList patches = ocean_mesh_clipmap_patches(ocean_config_);
        const cubey::render::GraphicsPipelineResource& ocean_pipeline = pipeline();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, ocean_pipeline.pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, ocean_pipeline.layout(), 0,
                                     ocean_gpu_.surface_set());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, ocean_pipeline.layout(), 1,
                                     scene_set);
        for (const OceanMeshPatch& patch : patches) {
            const OceanPushConstants constants = push_constants(extent, patch);
            recorder.push_constants(ocean_pipeline.layout(),
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    constants);
            recorder.draw(ocean_mesh_patch_vertex_count(patch));
        }
    }

    void record_ocean_sky(const cubey::vulkan::CommandRecorder& recorder, VkExtent2D extent) const {
        const OceanSkyPushConstants constants = sky_push_constants(extent);
        const cubey::render::GraphicsPipelineResource& sky_pipeline = ocean_gpu_.sky_pipeline();
        cubey::render::record_fullscreen_pipeline_draw(
            recorder,
            {
                .pipeline = &sky_pipeline,
            },
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, constants);
    }

    void record_ocean_scene(const cubey::vulkan::CommandRecorder& recorder,
                            VkExtent2D extent) const {
        const OceanScenePushConstants constants = scene_push_constants(extent);
        const cubey::render::GraphicsPipelineResource& scene_pipeline = ocean_gpu_.scene_pipeline();
        cubey::render::record_fullscreen_pipeline_draw(
            recorder,
            {
                .pipeline = &scene_pipeline,
            },
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, constants);
    }

    [[nodiscard]] OceanFrameGraph
    build_ocean_frame_graph(cubey::render::ColorTargetView color_target,
                            cubey::render::FrameSlot frame_slot,
                            OceanRenderTargetMode target_mode) {
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
            graph.create_texture(ocean_color_texture_desc("ocean scene color", color_target.extent,
                                                          color_target.format));
        const cubey::render::RenderGraphTextureHandle scene_depth =
            graph.create_texture(ocean_depth_texture_desc("ocean scene depth", color_target.extent,
                                                          kOceanSceneDepthFormat));

        graph.add_pass("ocean scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(scene_depth)
            .execute([this, scene_color,
                      scene_depth](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView depth =
                    cubey::render::resolved_depth_target_view(context, scene_depth);
                cubey::render::record_render_target_pass(
                    context.recorder(), cubey::render::render_target_view(target, depth),
                    cubey::render::RenderClearValues{
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
                    [this, target](const cubey::vulkan::CommandRecorder& draw_recorder) {
                        record_ocean_scene(draw_recorder, target.extent);
                    });
            });

        graph.add_pass("ocean final", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(scene_color, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
            .read_texture(scene_depth, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
            .write_color(backbuffer)
            .execute([this, backbuffer,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, backbuffer);
                cubey::render::record_render_target_pass(
                    context.recorder(), cubey::render::render_target_view(target),
                    cubey::render::RenderClearValues{
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                    },
                    [this, target,
                     frame_slot](const cubey::vulkan::CommandRecorder& draw_recorder) {
                        record_ocean_sky(draw_recorder, target.extent);
                        record_ocean_draw(draw_recorder, target.extent,
                                          ocean_gpu_.scene_set(frame_slot));
                    });
            });

        return {
            .graph = graph.compile(),
            .backbuffer = backbuffer,
            .scene_color = scene_color,
            .scene_depth = scene_depth,
        };
    }

    [[nodiscard]] OceanSpectrumPushConstants spectrum_push_constants(std::uint32_t cascade) const {
        const float wind = radians(ocean_config_.wind_direction_degrees);
        return {
            .ocean_options =
                {
                    static_cast<float>(ocean_config_.spectrum_resolution),
                    static_cast<float>(time_seconds_),
                    ocean_config_.wave_amplitude,
                    ocean_config_.spectrum_energy,
                },
            .cascade_options =
                {
                    ocean_cascade_patch_length(ocean_config_, cascade),
                    static_cast<float>(cascade),
                    ocean_config_.spectrum_fetch,
                    ocean_config_.chop,
                },
            .wind_options =
                {
                    std::cos(wind),
                    std::sin(wind),
                    ocean_config_.sea_state,
                    ocean_config_.swell_scale,
                },
            .seed_options =
                {
                    static_cast<float>(ocean_config_.spectrum_seed),
                    ocean_config_.foam_generation,
                    ocean_config_.foam_decay,
                    ocean_config_.animation_speed,
                },
        };
    }

    [[nodiscard]] OceanFftPushConstants fft_push_constants(std::uint32_t stage, bool horizontal,
                                                           bool first_pass) const {
        return {
            .fft_options =
                {
                    static_cast<float>(ocean_config_.spectrum_resolution),
                    static_cast<float>(stage),
                    horizontal ? 1.0F : 0.0F,
                    first_pass ? 1.0F : 0.0F,
                },
            .pass_options =
                {
                    0.0F,
                    0.0F,
                    0.0F,
                    0.0F,
                },
        };
    }

    [[nodiscard]] OceanFinalizePushConstants finalize_push_constants(std::uint32_t cascade) const {
        const float resolution = static_cast<float>(ocean_config_.spectrum_resolution);
        return {
            .ocean_options =
                {
                    resolution,
                    static_cast<float>(time_seconds_),
                    ocean_config_.wave_amplitude,
                    ocean_config_.normal_strength,
                },
            .cascade_options =
                {
                    ocean_cascade_patch_length(ocean_config_, cascade),
                    static_cast<float>(cascade),
                    ocean_config_.chop,
                    1.0F / (resolution * resolution),
                },
            .foam_options =
                {
                    ocean_config_.foam_generation,
                    ocean_config_.foam_decay,
                    ocean_config_.foam_threshold,
                    ocean_config_.foam_amount,
                },
            .debug_options =
                {
                    0.0F,
                    0.0F,
                    0.0F,
                    0.0F,
                },
        };
    }

    [[nodiscard]] OceanFoamUpdatePushConstants
    foam_update_push_constants(std::uint32_t cascade) const {
        const float wind = radians(ocean_config_.wind_direction_degrees);
        const double delta_seconds = last_delta_seconds_ > 0.0 ? last_delta_seconds_ : (1.0 / 60.0);
        return {
            .foam_options =
                {
                    static_cast<float>(delta_seconds),
                    ocean_config_.foam_decay,
                    ocean_config_.foam_generation * ocean_config_.foam_amount,
                    foam_initialized_ ? 1.0F : 0.0F,
                },
            .cascade_options =
                {
                    ocean_cascade_patch_length(ocean_config_, cascade),
                    std::cos(wind),
                    std::sin(wind),
                    ocean_config_.foam_drift,
                },
        };
    }

    [[nodiscard]] OceanDetailPushConstants detail_push_constants(std::uint32_t cascade) const {
        const float wind = radians(ocean_config_.wind_direction_degrees);
        return {
            .ocean_options =
                {
                    static_cast<float>(ocean_config_.spectrum_resolution),
                    static_cast<float>(time_seconds_),
                    ocean_config_.normal_strength,
                    ocean_config_.foam_threshold,
                },
            .cascade_options =
                {
                    ocean_cascade_patch_length(ocean_config_, cascade),
                    static_cast<float>(cascade),
                    ocean_config_.detail_chop,
                    ocean_config_.detail_spread,
                },
            .wind_options =
                {
                    std::cos(wind),
                    std::sin(wind),
                    ocean_config_.animation_speed,
                    ocean_config_.foam_breakup,
                },
            .foam_options =
                {
                    ocean_config_.foam_amount,
                    ocean_config_.sea_state,
                    ocean_config_.detail_geometry,
                    ocean_config_.crest_sharpness,
                },
        };
    }

    void record_initial_texture_transitions(const cubey::vulkan::CommandRecorder& recorder) const {
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.h0(cascade).handle()));
            for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(
                        ocean_gpu_.spectrum(cascade, field).handle()));
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
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.detail_normal_foam(cascade).handle()));
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.foam_history(cascade, 0).handle()));
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.foam_history(cascade, 1).handle()));
        }
    }

    void record_spectrum_init(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_spectrum_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(ocean_gpu_.spectrum_init_pipeline(),
                                                              ocean_gpu_.spectrum_init_set(cascade),
                                                              groups),
                VK_SHADER_STAGE_COMPUTE_BIT, spectrum_push_constants(cascade));
        }
    }

    void record_spectrum_evolve(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_spectrum_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.spectrum_evolve_pipeline(), ocean_gpu_.spectrum_evolve_set(cascade),
                    groups),
                VK_SHADER_STAGE_COMPUTE_BIT, spectrum_push_constants(cascade));
        }
    }

    void record_fft_pass(const cubey::vulkan::CommandRecorder& recorder, std::uint32_t cascade,
                         std::uint32_t field, std::uint32_t stage, bool horizontal, bool first_pass,
                         std::uint32_t descriptor_set_index) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_spectrum_dispatch_groups(ocean_config_);
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                ocean_gpu_.fft_pipeline(), ocean_gpu_.fft_set(cascade, field, descriptor_set_index),
                groups),
            VK_SHADER_STAGE_COMPUTE_BIT, fft_push_constants(stage, horizontal, first_pass));
    }

    void record_fft(const cubey::vulkan::CommandRecorder& recorder) const {
        const std::uint32_t stage_count = log2_exact(ocean_config_.spectrum_resolution);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
                for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                    const std::uint32_t set_index =
                        stage == 1U ? 0U : ((stage % 2U) == 0U ? 1U : 2U);
                    record_fft_pass(recorder, cascade, field, stage, true, stage == 1U, set_index);
                    cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                }
                for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                    const std::uint32_t set_index =
                        stage == 1U ? 2U : ((stage % 2U) == 0U ? 1U : 2U);
                    record_fft_pass(recorder, cascade, field, stage, false, stage == 1U, set_index);
                    cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                }
            }
        }
    }

    void record_finalize(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_spectrum_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.finalize_pipeline(), ocean_gpu_.finalize_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, finalize_push_constants(cascade));
        }
    }

    void record_detail_update(const cubey::vulkan::CommandRecorder& recorder) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_spectrum_dispatch_groups(ocean_config_);
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.detail_pipeline(), ocean_gpu_.detail_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, detail_push_constants(cascade));
        }
    }

    void record_foam_update(const cubey::vulkan::CommandRecorder& recorder) {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_spectrum_dispatch_groups(ocean_config_);
        const std::uint32_t previous_history_index = foam_history_index_;
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.foam_update_pipeline(),
                    ocean_gpu_.foam_update_set(cascade, previous_history_index), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, foam_update_push_constants(cascade));
        }
        foam_history_index_ = 1U - previous_history_index;
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
        record_spectrum_evolve(recorder);
        cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
        record_fft(recorder);
        record_finalize(recorder);
        cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
        record_detail_update(recorder);
        cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
        record_foam_update(recorder);
        cubey::vulkan::record_shader_write_barrier(recorder.handle(),
                                                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                   VK_ACCESS_SHADER_READ_BIT);
    }

    void record_ocean_target(VkCommandBuffer command_buffer, const cubey::vulkan::Device& device,
                             cubey::render::ColorTargetView target,
                             cubey::render::FrameSlot frame_slot,
                             OceanRenderTargetMode target_mode) {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        record_ocean_compute(recorder);
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
             &frame_graph](const cubey::render::RenderGraphResourceSet& graph_resources) {
                ocean_gpu_.update_scene_descriptors(
                    device, frame_slot,
                    cubey::render::resolved_sampled_texture_view(frame_graph.graph, graph_resources,
                                                                 frame_graph.scene_color),
                    cubey::render::resolved_sampled_texture_view(frame_graph.graph, graph_resources,
                                                                 frame_graph.scene_depth));
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
    OceanRenderView render_view_ = ocean_config_.render_view;
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
    bool paused_ = false;
    bool reset_requested_ = false;
    bool textures_initialized_ = false;
    bool spectrum_initialized_ = false;
    bool foam_initialized_ = false;
    std::uint32_t foam_history_index_ = 0;
};

} // namespace

int run_ocean(const RunConfig& config) {
    OceanApp app(config);
    return app.run();
}

} // namespace cubey::projects::ocean
