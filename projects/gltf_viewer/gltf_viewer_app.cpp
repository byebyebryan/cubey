#include "gltf_viewer_app_internal.h"

#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/cloud_environment_ui.h>
#include <cubey/host/imgui_helpers.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/scene/transform_3d.h>

#include <imgui.h>
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
        cubey::render::atmosphere_environment_radians_to_degrees(std::atan2(normal.x, -normal.z)));
}

} // namespace

const cubey::math::Vec3 kLightDirection = glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
gltf_viewer_atmosphere_run_state(const GltfViewerStartupOptions& run_config) {
    return cubey::atmosphere_environment_run_state_from_config(
        run_config.atmosphere,
        {
            .sun_elevation_degrees = direction_elevation_degrees(kLightDirection),
            .sun_azimuth_degrees = direction_azimuth_degrees(kLightDirection),
            .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
            .reference_geometry_enabled = false,
        });
}

[[nodiscard]] cubey::CloudEnvironmentConfig gltf_viewer_cloud_config(const GltfViewerStartupOptions& run_config) {
    cubey::CloudEnvironmentConfig clouds{};
    cubey::apply_cloud_environment_options(clouds, run_config.clouds);
    clouds.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    clouds.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    clouds.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Local;
    return clouds;
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

GltfViewerApp::GltfViewerApp(GltfViewerProjectConfig config)
    : config_(std::move(config)), debug_view_(render::pbr_debug_view_from_name(config_.debug_view)),
      atmosphere_state_(gltf_viewer_atmosphere_run_state(config_)),
      clouds_config_(gltf_viewer_cloud_config(config_)),
      ocean_config_(gltf_viewer_ocean_config_from_options(config_)) {
    if (terrain_backdrop_enabled() && ocean_backdrop_enabled()) {
        throw std::runtime_error(
            "glTF viewer v1 accepts either terrain or ocean backdrop, not both");
    }
    atmosphere_runtime_.set_environment(atmosphere_state_.environment);
    if (config_.terrain.foreground_height_m) {
        terrain_foreground_height_m_ = *config_.terrain.foreground_height_m;
    }
    terrain_shadows_ = config_.terrain.shadows.value_or(false);
    if (config_.terrain.surface_detail && *config_.terrain.surface_detail == "flat") {
        terrain_material_ = cubey::render::TerrainBackdropMaterialMode::Flat;
    }
    if (config_.ocean.foreground_height_m) {
        ocean_foreground_height_m_ = *config_.ocean.foreground_height_m;
        ocean_foreground_height_explicit_ = true;
    }
}

bool GltfViewerApp::update_atmosphere_time(double delta_seconds) {
    if (!cubey::atmosphere_environment_advance_time(atmosphere_state_, delta_seconds)) {
        return false;
    }

    atmosphere_runtime_.set_environment(atmosphere_state_.environment);
    return true;
}

void GltfViewerApp::refresh_atmosphere_controls() {
    atmosphere_runtime_.set_environment(atmosphere_state_.environment);
    refresh_atmosphere_lighting_scene();
}

void GltfViewerApp::refresh_cloud_controls(cubey::host::WindowedAppContext& context) {
    cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    if (!use_atmosphere_environment_source() || !clouds.surface_resources_created()) {
        return;
    }
    clouds.set_config(cloud_environment_config());
    const cubey::render::CloudLayerRuntimeShaderFiles shaders =
        cubey::render::cloud_layer_runtime_shader_files(
            CUBEY_GLTF_VIEWER_SHADER_DIR,
            cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
    clouds.update_weather_texture(context.device(), context.gpu(), shaders.generated.weather);
    clouds.invalidate();
}

void GltfViewerApp::draw_ui(cubey::host::WindowedAppContext& context) {
    if (!cubey::host::begin_control_panel("glTF Viewer", {.width = 430.0F})) {
        ImGui::End();
        return;
    }

    if (cubey::host::draw_atmosphere_environment_controls(
            atmosphere_state_, {.default_open = true,
                                .help = "Shared procedural atmosphere used by the glTF viewer sky, "
                                        "lighting, PBR environment, and exposure."})) {
        refresh_atmosphere_controls();
    }
    if (use_atmosphere_environment_source() &&
        cubey::host::draw_cloud_environment_controls(
            clouds_config_,
            {.label = "Cloud Environment",
             .default_open = false,
             .help = "Shared surface clouds composed into the procedural sky and cached for PBR "
                     "reflections.",
             .enabled_help = "Render direct clouds with scene-depth occlusion and include them in "
                             "PBR environment reflections."})) {
        refresh_cloud_controls(context);
    }
    if (terrain_backdrop_enabled() && ImGui::CollapsingHeader("Terrain Backdrop")) {
        ImGui::Checkbox("Visible", &terrain_visible_);
        ImGui::SliderFloat("Foreground height", &terrain_foreground_height_m_,
                           terrain_minimum_foreground_height_m_,
                           std::max(1'000.0F, terrain_minimum_foreground_height_m_ * 2.0F),
                           "%.0f m");
        ImGui::Checkbox("Terrain shadows", &terrain_shadows_);
        ImGui::Checkbox("Foreground reflections", &terrain_reflections_);
        int material =
            terrain_material_ == cubey::render::TerrainBackdropMaterialMode::FilteredDetail ? 1 : 0;
        if (ImGui::RadioButton("Flat", material == 0)) {
            terrain_material_ = cubey::render::TerrainBackdropMaterialMode::Flat;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Filtered detail", material == 1)) {
            terrain_material_ = cubey::render::TerrainBackdropMaterialMode::FilteredDetail;
        }
    }
    if (ocean_backdrop_enabled() && ImGui::CollapsingHeader("Ocean Backdrop")) {
        ImGui::Checkbox("Visible##ocean", &ocean_visible_);
        ImGui::SliderFloat("Foreground height##ocean", &ocean_foreground_height_m_,
                           std::max(ocean_minimum_foreground_height_m_, 0.01F),
                           std::max(10'000.0F, ocean_minimum_foreground_height_m_ * 2.0F), "%.0f m",
                           ImGuiSliderFlags_Logarithmic);
        cubey::render::OceanSeaState sea_state = ocean_config_.sea_state;
        if (cubey::host::imgui_enum_combo("Sea state", sea_state,
                                          cubey::render::kOceanSeaStatePresets,
                                          cubey::render::ocean_sea_state_name)) {
            cubey::render::apply_ocean_sea_state(ocean_config_, sea_state);
            ocean_runtime_.set_config(ocean_config_);
            const cubey::render::BackdropSurfacePlacement placement =
                cubey::render::resolve_backdrop_surface_placement({
                    .surface =
                        {
                            .maximum_local_height_m =
                                cubey::render::ocean_surface_placement_crest_allowance_m(
                                    ocean_config_),
                        },
                    .foreground =
                        {
                            .anchor_world_height_m = scene_bounds_.center.y,
                            .minimum_local_height_m = -scene_bounds_.half_extent.y,
                        },
                    .requested_foreground_height_m = ocean_foreground_height_m_,
                    .minimum_clearance_m = 0.1F,
                });
            ocean_minimum_foreground_height_m_ = placement.required_foreground_height_m;
            ocean_foreground_height_m_ = placement.effective_foreground_height_m;
        }
    }

    ImGui::End();
}

int GltfViewerApp::run() {
    if (config_.common.headless) {
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
                               context.swapchain().format(), context.frame_slot_count());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        poll_atmosphere_background_atlases(context.device(), context.gpu(),
                                           context.frame_resources());
        update_animation(static_cast<float>(timing.delta_seconds));
        if (update_atmosphere_time(timing.delta_seconds)) {
            refresh_atmosphere_lighting_scene();
        }
        atmosphere_runtime_.advance(timing.delta_seconds);
        ocean_delta_seconds_ = timing.delta_seconds > 0.0 ? timing.delta_seconds : (1.0 / 60.0);
        ocean_elapsed_seconds_ += ocean_delta_seconds_;
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::D)) {
            debug_view_ = render::next_pbr_debug_view(debug_view_);
        }
        orbit_controller_.update_from_input(input, timing.delta_seconds);
        update_camera_transform();
    };
    callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
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
        destroy_all_resources(context.gpu());
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_.common,
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
    host_config.run_config = config_.common;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    host_config.require_dynamic_rendering = true;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          cubey::host::headless_capture_frame_slot_count(config_.common));
        finish_atmosphere_background_atlases(context.device(), context.gpu());
        create_frame_resources(context.device(), context.render_target().extent,
                               context.render_target().format,
                               cubey::host::headless_capture_frame_slot_count(config_.common));
    };
    if (config_.common.capture_mode == CaptureMode::Video) {
        orbit_controller_.set_auto_rotation_speed(kHeadlessVideoOrbitSpeed);
        callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                        const cubey::host::HeadlessCaptureFrame& frame) {
            update_animation(static_cast<float>(frame.timing.delta_seconds));
            if (update_atmosphere_time(frame.timing.delta_seconds)) {
                refresh_atmosphere_lighting_scene();
            }
            atmosphere_runtime_.advance(frame.timing.delta_seconds);
            ocean_delta_seconds_ =
                frame.timing.delta_seconds > 0.0 ? frame.timing.delta_seconds : (1.0 / 60.0);
            ocean_elapsed_seconds_ = frame.timing.elapsed_seconds;
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
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext& context) {
        destroy_all_resources(context.gpu());
    };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

int run_gltf_viewer(const GltfViewerProjectConfig& config) {
    GltfViewerApp app(config);
    return app.run();
}

} // namespace cubey::projects::gltf_viewer
