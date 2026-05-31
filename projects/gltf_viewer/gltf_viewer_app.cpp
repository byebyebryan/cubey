#include "gltf_viewer_app_internal.h"

#include <cubey/render/primitive_mesh.h>
#include <cubey/scene/transform_3d.h>

#include <vulkan/vulkan.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

#ifndef CUBEY_GLTF_VIEWER_SHADER_DIR
#error "CUBEY_GLTF_VIEWER_SHADER_DIR must be defined by the gltf_viewer CMake target"
#endif

namespace cubey::projects::gltf_viewer {

using cubey::host::FrameStatsSample;

namespace {

constexpr float kHeadlessVideoOrbitSpeed = 0.32F;

[[nodiscard]] float direction_elevation_degrees(cubey::math::Vec3 direction) {
    return cubey::render::atmosphere_environment_radians_to_degrees(
        std::asin(std::clamp(glm::normalize(direction).y, -1.0F, 1.0F)));
}

[[nodiscard]] float direction_azimuth_degrees(cubey::math::Vec3 direction) {
    const cubey::math::Vec3 normal = glm::normalize(direction);
    return cubey::render::atmosphere_environment_wrap_signed_degrees(
        cubey::render::atmosphere_environment_radians_to_degrees(
            std::atan2(normal.x, -normal.z)));
}

[[nodiscard]] bool gltf_viewer_uses_solar_time(const RunConfig& run_config) {
    const RunConfig::AtmosphereOptions& atmosphere = run_config.atmosphere;
    if (atmosphere.time_of_day_mode == "solar") {
        return true;
    }
    if (atmosphere.time_of_day_mode == "manual") {
        return false;
    }
    if (run_config_float_is_set(atmosphere.sun_elevation_degrees) ||
        run_config_float_is_set(atmosphere.sun_azimuth_degrees)) {
        return false;
    }
    return run_config_float_is_set(atmosphere.time_hours) ||
           run_config_float_is_set(atmosphere.day_of_year) ||
           run_config_float_is_set(atmosphere.latitude_degrees) ||
           run_config_float_is_set(atmosphere.sun_azimuth_offset_degrees) ||
           run_config_float_is_set(atmosphere.time_speed_hours_per_second);
}

[[nodiscard]] float gltf_viewer_atmosphere_time_speed(const RunConfig& run_config) {
    return run_config_float_is_set(run_config.atmosphere.time_speed_hours_per_second)
               ? run_config.atmosphere.time_speed_hours_per_second
               : 0.0F;
}

[[nodiscard]] bool gltf_viewer_atmosphere_time_playing(const RunConfig& run_config) {
    return run_config.atmosphere.time_paused != 1 &&
           gltf_viewer_atmosphere_time_speed(run_config) > 0.0F;
}

[[nodiscard]] cubey::render::AtmosphereDiffuseSource
gltf_viewer_atmosphere_diffuse_source(const RunConfig& run_config) {
    if (run_config.pbr.diffuse_source == "irradiance") {
        return cubey::render::AtmosphereDiffuseSource::IrradianceCube;
    }
    return cubey::render::AtmosphereDiffuseSource::SphericalHarmonics;
}

} // namespace

const cubey::math::Vec3 kLightDirection = glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
gltf_viewer_atmosphere_environment_config(const RunConfig& run_config) {
    cubey::render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = direction_elevation_degrees(kLightDirection);
    environment.sun_azimuth_degrees = direction_azimuth_degrees(kLightDirection);
    environment.ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnly;
    environment.reference_geometry_enabled = false;

    const RunConfig::AtmosphereOptions& atmosphere = run_config.atmosphere;
    if (run_config_float_is_set(atmosphere.time_hours)) {
        environment.time_of_day.time_hours = atmosphere.time_hours;
    }
    if (run_config_float_is_set(atmosphere.day_of_year)) {
        environment.time_of_day.day_of_year = atmosphere.day_of_year;
    }
    if (run_config_float_is_set(atmosphere.latitude_degrees)) {
        environment.time_of_day.latitude_degrees = atmosphere.latitude_degrees;
    }
    if (run_config_float_is_set(atmosphere.sun_azimuth_offset_degrees)) {
        environment.time_of_day.azimuth_offset_degrees = atmosphere.sun_azimuth_offset_degrees;
    }

    if (gltf_viewer_uses_solar_time(run_config)) {
        const cubey::render::AtmosphereEnvironmentSolarPosition solar =
            cubey::render::atmosphere_environment_solar_position(environment.time_of_day);
        environment.sun_elevation_degrees = solar.elevation_degrees;
        environment.sun_azimuth_degrees = solar.azimuth_degrees;
    } else {
        if (run_config_float_is_set(atmosphere.sun_elevation_degrees)) {
            environment.sun_elevation_degrees = atmosphere.sun_elevation_degrees;
        }
        if (run_config_float_is_set(atmosphere.sun_azimuth_degrees)) {
            environment.sun_azimuth_degrees = atmosphere.sun_azimuth_degrees;
        }
    }

    if (run_config_float_is_set(atmosphere.camera_altitude_km)) {
        environment.camera_altitude_km = atmosphere.camera_altitude_km;
    }
    if (run_config_float_is_set(atmosphere.mie_scale)) {
        environment.mie_density_scale = atmosphere.mie_scale;
    }
    if (run_config_float_is_set(atmosphere.moonlight_intensity)) {
        environment.moon.moonlight_intensity = atmosphere.moonlight_intensity;
    }
    if (run_config_float_is_set(atmosphere.moon_intensity)) {
        environment.moon.disk_intensity = atmosphere.moon_intensity;
    }
    if (run_config_float_is_set(atmosphere.moon_phase_offset_days)) {
        environment.moon.phase_offset_days = atmosphere.moon_phase_offset_days;
    }
    if (run_config_float_is_set(atmosphere.moon_size_scale)) {
        environment.moon.angular_radius_scale = atmosphere.moon_size_scale;
    }
    if (atmosphere.moon >= 0) {
        environment.moon.enabled = atmosphere.moon == 1;
    }

    return environment;
}

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_GLTF_VIEWER_SHADER_DIR) / filename;
}

std::filesystem::path bundled_sample_asset_path() {
#ifdef CUBEY_GLTF_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_GLTF_SAMPLE_ASSETS_DIR) / "Models" / "DamagedHelmet" /
           "glTF-Binary" / "DamagedHelmet.glb";
#else
    return {};
#endif
}

std::filesystem::path bundled_sample_environment_path() {
#ifdef CUBEY_HDR_SAMPLE_ASSETS_DIR
    return std::filesystem::path(CUBEY_HDR_SAMPLE_ASSETS_DIR) / "lightroom_14b.hdr";
#else
    return {};
#endif
}

cubey::ForwardPbrRenderer3DConfig forward_pbr_renderer_3d_config() {
    return cubey::forward_pbr_renderer_3d_config_from_shader_directory(
        CUBEY_GLTF_VIEWER_SHADER_DIR, {.shadow_extent = kShadowMapSize});
}

cubey::Transform3D look_at_transform(cubey::math::Vec3 eye, cubey::math::Vec3 target) {
    const cubey::math::Vec3 forward = glm::normalize(target - eye);
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    if (std::abs(glm::dot(forward, up)) > 0.95F) {
        up = {0.0F, 0.0F, 1.0F};
    }
    return {
        .translation = eye,
        .rotation = glm::quatLookAtRH(forward, up),
    };
}

std::vector<cubey::render::PbrVertex> fallback_cube_vertices() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();
    std::vector<cubey::render::PbrVertex> vertices;
    vertices.reserve(cube.vertices.size());
    for (const cubey::render::VertexPositionColorNormalUv& vertex : cube.vertices) {
        vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return vertices;
}

std::vector<std::uint32_t> fallback_cube_indices() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();
    std::vector<std::uint32_t> indices;
    indices.reserve(cube.indices.size());
    for (const std::uint16_t index : cube.indices) {
        indices.push_back(index);
    }
    return indices;
}

GltfViewerApp::GltfViewerApp(RunConfig config)
    : config_(std::move(config)),
      debug_view_(render::pbr_debug_view_from_name(config_.debug_view)),
      atmosphere_diffuse_source_(gltf_viewer_atmosphere_diffuse_source(config_)),
      atmosphere_solar_time_enabled_(gltf_viewer_uses_solar_time(config_)),
      atmosphere_time_playing_(gltf_viewer_atmosphere_time_playing(config_)),
      atmosphere_time_speed_hours_per_second_(gltf_viewer_atmosphere_time_speed(config_)) {
    atmosphere_runtime_.set_environment(gltf_viewer_atmosphere_environment_config(config_));
}

bool GltfViewerApp::update_atmosphere_time(double delta_seconds) {
    if (!atmosphere_time_playing_ || atmosphere_time_speed_hours_per_second_ <= 0.0F ||
        delta_seconds <= 0.0) {
        return false;
    }

    cubey::render::AtmosphereEnvironmentConfig environment = atmosphere_runtime_.environment();
    const double current_time_hours = static_cast<double>(environment.time_of_day.time_hours);
    const double next_time_hours =
        current_time_hours +
        (static_cast<double>(atmosphere_time_speed_hours_per_second_) * delta_seconds);
    const int day_delta = static_cast<int>(std::floor(next_time_hours / 24.0));
    environment.time_of_day.time_hours =
        cubey::render::atmosphere_environment_wrap_time_hours(
            static_cast<float>(next_time_hours));
    if (day_delta != 0) {
        environment.time_of_day.day_of_year =
            cubey::render::atmosphere_environment_advance_day_of_year(
                environment.time_of_day.day_of_year, day_delta);
    }

    if (atmosphere_solar_time_enabled_) {
        const cubey::render::AtmosphereEnvironmentSolarPosition solar =
            cubey::render::atmosphere_environment_solar_position(
                environment.time_of_day);
        environment.sun_elevation_degrees = solar.elevation_degrees;
        environment.sun_azimuth_degrees = solar.azimuth_degrees;
    }

    atmosphere_runtime_.set_environment(environment);
    return true;
}

int GltfViewerApp::run() {
    if (config_.headless) {
        return run_headless();
    }
    return run_windowed();
}

int GltfViewerApp::run_windowed() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          context.frame_slot_count());
        create_frame_resources(context.device(), context.swapchain().extent(),
                               context.swapchain().format());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        update_animation(static_cast<float>(timing.delta_seconds));
        if (update_atmosphere_time(timing.delta_seconds)) {
            refresh_atmosphere_lighting_scene();
        }
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::D)) {
            debug_view_ = render::next_pbr_debug_view(debug_view_);
        }
        orbit_controller_.update_from_input(input, timing.delta_seconds);
        update_camera_transform();
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_viewer_frame(context, frame);
    };
    callbacks.frame_stats_sample =
        [this](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        return FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = triangle_count_,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_,
            .app_name = "gltf_viewer",
            .ready_status = "rendering glTF/PBR viewer",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int GltfViewerApp::run_headless() {
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    host_config.require_dynamic_rendering = true;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          cubey::host::headless_capture_frame_slot_count(config_));
        create_frame_resources(context.device(), context.render_target().extent,
                               context.render_target().format);
    };
    if (config_.capture_mode == CaptureMode::Video) {
        orbit_controller_.set_auto_rotation_speed(kHeadlessVideoOrbitSpeed);
        callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                        const cubey::host::HeadlessCaptureFrame& frame) {
            update_animation(static_cast<float>(frame.timing.delta_seconds));
            if (update_atmosphere_time(frame.timing.delta_seconds)) {
                refresh_atmosphere_lighting_scene();
            }
            orbit_controller_.update(frame.timing.delta_seconds);
            update_camera_transform();
        };
    }
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                    const cubey::host::HeadlessCaptureFrame& frame,
                                    VkCommandBuffer command_buffer,
                                    const cubey::host::HeadlessRenderTarget& target) {
        record_viewer_capture(context, frame, command_buffer, target);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

int run_gltf_viewer(const RunConfig& config) {
    GltfViewerApp app(config);
    return app.run();
}

} // namespace cubey::projects::gltf_viewer
