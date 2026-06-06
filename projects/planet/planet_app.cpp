#include "planet_app.h"

#include "planet_camera.h"
#include "planet_celestial.h"
#include "planet_config.h"
#include "planet_frame.h"
#include "planet_surface.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/performance_ui.h>
#include <cubey/host/windowed_app.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/instance_buffer.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/vk_check.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_PLANET_SHADER_DIR
#error "CUBEY_PLANET_SHADER_DIR must be defined by the planet target"
#endif

namespace cubey::projects::planet {
namespace {

constexpr const char* kAppName = "planet";
constexpr const char* kReadyStatus = "rendering planet project";
constexpr float kPlanetCameraBaseYaw = 0.55F;
constexpr float kPlanetCameraBasePitch = 0.28F;
constexpr float kPlanetMoonAngularRadiusScale = 4.0F;
constexpr float kPlanetMoonShellDistanceFraction = 0.88F;
constexpr std::uint32_t kPlanetSurfaceFrameUniformBinding = 0;
constexpr VkFormat kPlanetSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

struct PlanetSurfaceFrameUniforms {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_debug{0.35F, 0.78F, 0.50F, 0.0F};
    cubey::math::Vec4 render_origin_radius{0.0F, 0.0F, 0.0F, kPlanetDefaultRadiusM};
    cubey::math::Vec4 surface_options{0.0F, 0.0F, 1.0F, 0.0F};
    cubey::math::Vec4 terrain_options{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 field_options{0.0F, kPlanetDefaultBathymetryDepthScaleM,
                                    kPlanetDefaultShorelineWidthM, 0.0F};
    cubey::math::Vec4 camera_horizon{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 atmosphere_options{0.14F, 0.42F, 0.45F, 1.0F};
    cubey::math::Vec4 haze_color_direct{0.18F, 0.28F, 0.44F, 0.86F};
    cubey::math::Vec4 celestial_equator_plane{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_ecliptic_plane{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_moon_orbit_plane{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_sun_direction{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_moon_direction{0.0F, 0.0F, 1.0F, 0.0F};
    cubey::math::Vec4 camera_world_radius{0.0F, 0.0F, 0.0F,
                                          kPlanetDefaultRadiusM + kPlanetDefaultCameraAltitudeM};
    cubey::math::Vec4 atmosphere_radius_mode{
        kPlanetDefaultRadiusM + kPlanetDefaultAtmosphereHeightM,
        kPlanetDefaultAtmosphereHeightM,
        static_cast<float>(static_cast<std::uint32_t>(PlanetAtmosphereMode::Analytic)),
        0.004675F};
    cubey::math::Vec4 sun_color_intensity{1.0F, 0.94F, 0.82F, 0.88F};
};

static_assert(sizeof(PlanetSurfaceFrameUniforms) == sizeof(float) * 4U * 20U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PLANET_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo planet_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "planet.forward",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = kPlanetSurfaceFrameUniformBinding,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] cubey::render::VertexInputLayout planet_surface_vertex_input_layout() {
    cubey::render::VertexInputLayout layout;
    layout.vertex_bindings.push_back(cubey::render::vertex_input_binding(
        0, static_cast<std::uint32_t>(sizeof(PlanetPatchGridVertex)), VK_VERTEX_INPUT_RATE_VERTEX));
    layout.vertex_bindings.push_back(
        cubey::render::instance_input_binding<PlanetSurfaceGpuPatchInstance>(1));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PlanetPatchGridVertex, uv)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        1, 0, VK_FORMAT_R32_SFLOAT, offsetof(PlanetPatchGridVertex, skirt)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        2, 1, VK_FORMAT_R32G32B32A32_UINT, offsetof(PlanetSurfaceGpuPatchInstance, face)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        3, 1, VK_FORMAT_R32_SFLOAT, offsetof(PlanetSurfaceGpuPatchInstance, screen_error_px)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        4, 1, VK_FORMAT_R32_UINT, offsetof(PlanetSurfaceGpuPatchInstance, edge_transition_mask)));
    return layout;
}

[[nodiscard]] float packed_debug_wire_option(const PlanetConfig& config) {
    return static_cast<float>(static_cast<int>(config.debug_view)) +
           (config.wire_overlay ? 0.25F : 0.0F);
}

[[nodiscard]] float packed_patch_lod_option(const PlanetConfig& config) {
    return static_cast<float>(config.patch_resolution * 256U + config.patches_per_face * 16U +
                              config.max_lod_level);
}

[[nodiscard]] float packed_terrain_detail_strengths(const PlanetConfig& config) {
    constexpr float kQuantizeScale = 1024.0F;
    constexpr float kQuantizeBase = 4096.0F;
    const float mid = std::clamp(std::round(config.terrain_mid_detail_strength * kQuantizeScale),
                                 0.0F, kQuantizeBase - 1.0F);
    const float fine = std::clamp(std::round(config.terrain_fine_detail_strength * kQuantizeScale),
                                  0.0F, kQuantizeBase - 1.0F);
    return mid * kQuantizeBase + fine;
}

class PlanetApp {
  public:
    explicit PlanetApp(RunConfig config)
        : config_(std::move(config)), planet_config_(planet_config_from_run_config(config_)),
          edit_planet_config_(planet_config_), solar_time_(planet_solar_time_from_run_config(config_)),
          celestial_system_(planet_celestial_system_from_solar_time(solar_time_)),
          celestial_lighting_(planet_celestial_lighting(celestial_system_)),
          camera_state_(planet_camera_initial_state_from_run_config(
              planet_config_, config_, kPlanetCameraBaseYaw, kPlanetCameraBasePitch)) {
        refresh_frame();
        rebuild_surface_data(default_surface_extent());
    }

    int run() {
        if (config_.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    struct PatchInstanceBufferSlot {
        std::optional<cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>> buffer{};
        std::uint64_t generation = 0;
    };

    struct PlanetFrameGraph {
        cubey::render::CompiledRenderGraph graph{};
        cubey::render::RenderGraphTextureHandle scene_color{};
    };

    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_forward_pass(context.device(), context.swapchain().extent(),
                                context.swapchain().format(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_input(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_planet_frame(context.device(), context.gpu(), frame.command_buffer,
                                frame.color_target, frame.frame_slot, true);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = kAppName,
                .ready_status = kReadyStatus,
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
            create_global_resources_if_needed(
                context.device(), context.gpu(),
                cubey::host::headless_capture_frame_slot_count(config_));
            create_forward_pass(context.device(), context.render_target().extent,
                                context.render_target().format,
                                cubey::host::headless_capture_frame_slot_count(config_));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            record_planet_frame(context.device(), context.gpu(), command_buffer, target,
                                frame.frame_slot, false);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (!patch_grid_mesh_.has_value()) {
            patch_grid_mesh_.emplace(gpu, patch_grid_.mesh_config());
        }
        if (!moon_mesh_.has_value()) {
            const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv>
                moon_mesh = cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
                    .radius = 1.0F,
                    .latitude_segments = 32,
                    .longitude_segments = 64,
                    .color = {0.58F, 0.62F, 0.74F},
                });
            moon_mesh_.emplace(gpu, moon_mesh.mesh_config());
        }
        if (!surface_frame_material_.has_value() ||
            surface_frame_material_->material().set_count() != frame_slot_count) {
            surface_frame_material_.reset();
            surface_frame_material_.emplace(
                device, cubey::render::FrameUniformMaterialInstanceConfig{
                            .material_pass = planet_pass_info(),
                            .descriptor_set = 0,
                            .frame_slot_count = frame_slot_count,
                            .uniform_binding = kPlanetSurfaceFrameUniformBinding,
                        });
        }
        resize_patch_instance_buffer_slots(frame_slot_count);
        create_sky_frame_resources_if_needed(device, frame_slot_count);
        create_celestial_body_frame_resources_if_needed(device, frame_slot_count);
        create_hdr_post_resources_if_needed(device, frame_slot_count);
    }

    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format, std::uint32_t frame_slot_count) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("planet_surface.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("planet_surface.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input = planet_surface_vertex_input_layout();
        const std::array descriptor_set_layouts{surface_frame_material().layout()};
        forward_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{
                .extent = extent,
                .color_format = kPlanetSceneColorFormat,
            },
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .descriptor_set_layouts = descriptor_set_layouts,
                        .material_pass = planet_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.018F, 0.030F, 0.052F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });
        create_sky_frame_pipeline(device, extent, kPlanetSceneColorFormat);
        create_celestial_body_frame_pipeline(device, extent, kPlanetSceneColorFormat,
                                             forward_pass().depth_target().format);
        create_hdr_post_pipeline(device, extent, color_format);
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        hdr_post_frame_.destroy_pipeline();
        celestial_body_frame_.destroy_pipeline();
        sky_frame_.destroy_pipeline();
        forward_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        hdr_post_frame_.destroy();
        hdr_post_frame_slot_count_ = 0;
        celestial_body_frame_.destroy();
        sky_frame_.destroy();
        patch_instance_buffers_.clear();
        moon_mesh_.reset();
        patch_grid_mesh_.reset();
        surface_frame_material_.reset();
    }

    void update_input(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        update_solar_time(timing.delta_seconds);
        update_camera_input(context.filtered_input(), timing.delta_seconds);
        refresh_frame();
        if (patch_grid_mesh_.has_value() && surface_plan_changed(context.swapchain().extent())) {
            surface_rebuild_pending_ = true;
        }
        if (patch_grid_mesh_.has_value() && surface_rebuild_pending_ && !camera_interacting_) {
            rebuild_surface_resources(context.swapchain().extent());
            surface_rebuild_pending_ = false;
        }
    }

    void update_camera_input(const cubey::input::FilteredInputFrame& input, double delta_seconds) {
        camera_interacting_ = false;
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_camera();
            camera_interacting_ = true;
        }

        const double scroll_y = input.scroll_delta().y;
        if (scroll_y != 0.0) {
            planet_camera_zoom_by_scroll(camera_state_, planet_config_, scroll_y);
            camera_interacting_ = true;
        }

        const float surface_blend =
            planet_surface_camera_blend(planet_config_, planet_camera_distance_m(camera_state_));
        if (input.mouse_button_down(cubey::input::MouseButton::Right) && surface_blend >= 0.20F) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Right);
            planet_camera_surface_look_drag(camera_state_, planet_config_, delta.x, delta.y);
            camera_interacting_ = true;
        } else if (input.mouse_button_down(cubey::input::MouseButton::Left) &&
                   surface_blend < 0.85F) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Left);
            planet_camera_orbit_drag(camera_state_, planet_config_, delta.x, delta.y);
            camera_interacting_ = true;
        }

        float forward = 0.0F;
        float right = 0.0F;
        if (input.key_down(cubey::input::Key::W)) {
            forward += 1.0F;
        }
        if (input.key_down(cubey::input::Key::S)) {
            forward -= 1.0F;
        }
        if (input.key_down(cubey::input::Key::D)) {
            right += 1.0F;
        }
        if (input.key_down(cubey::input::Key::A)) {
            right -= 1.0F;
        }
        camera_interacting_ = planet_camera_surface_move(camera_state_, planet_config_, forward,
                                                         right, delta_seconds) ||
                              camera_interacting_;
        planet_camera_update_surface_mode(camera_state_, planet_config_);
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        ImGui::TextUnformatted("Planet");
        ImGui::SeparatorText("Controls");
        PlanetConfig config_before_edit = edit_planet_config_;
        ImGui::InputFloat("Radius (m)", &edit_planet_config_.radius_m, 0.0F, 0.0F, "%.0f");
        ImGui::InputFloat("Atmosphere Height (m)", &edit_planet_config_.atmosphere_height_m, 0.0F,
                          0.0F, "%.0f");
        ImGui::InputFloat("Camera Altitude (m)", &edit_planet_config_.camera_altitude_m, 0.0F, 0.0F,
                          "%.0f");
        int patches_per_face = static_cast<int>(edit_planet_config_.patches_per_face);
        if (ImGui::InputInt("Patches / Face", &patches_per_face)) {
            edit_planet_config_.patches_per_face =
                static_cast<std::uint32_t>(std::max(patches_per_face, 0));
        }
        int patch_resolution = static_cast<int>(edit_planet_config_.patch_resolution);
        if (ImGui::InputInt("Patch Grid Resolution", &patch_resolution)) {
            edit_planet_config_.patch_resolution = static_cast<std::uint32_t>(
                std::clamp(patch_resolution, 0, static_cast<int>(kPlanetMaxPatchResolution)));
        }
        int max_lod_level = static_cast<int>(edit_planet_config_.max_lod_level);
        if (ImGui::InputInt("Max LOD Level", &max_lod_level)) {
            edit_planet_config_.max_lod_level = static_cast<std::uint32_t>(
                std::clamp(max_lod_level, 0, static_cast<int>(kPlanetMaxLiveLodLevel)));
        }
        ImGui::InputFloat("LOD Target Edge (px)", &edit_planet_config_.lod_target_edge_px, 0.0F,
                          0.0F, "%.1f");
        ImGui::InputFloat("LOD Hysteresis", &edit_planet_config_.lod_hysteresis, 0.0F, 0.0F,
                          "%.2f");
        constexpr const char* kDebugViews[]{
            "final",         "face-id",          "patch-id",
            "lod-level",     "screen-error",     "lod-transition",
            "seams",         "cell-edge",        "terrain-height",
            "terrain-slope", "terrain-material", "bathymetry",
            "shoreline",     "wireframe",        "celestial-planes"};
        int debug_view = static_cast<int>(edit_planet_config_.debug_view);
        if (ImGui::Combo("Debug View", &debug_view, kDebugViews,
                         static_cast<int>(std::size(kDebugViews)))) {
            edit_planet_config_.debug_view = static_cast<PlanetDebugView>(debug_view);
        }
        constexpr const char* kAtmosphereModes[]{"analytic", "physical"};
        int atmosphere_mode = static_cast<int>(edit_planet_config_.atmosphere_mode);
        if (ImGui::Combo("Atmosphere Mode", &atmosphere_mode, kAtmosphereModes,
                         static_cast<int>(std::size(kAtmosphereModes)))) {
            edit_planet_config_.atmosphere_mode =
                static_cast<PlanetAtmosphereMode>(atmosphere_mode);
        }
        ImGui::Checkbox("Wire Overlay", &edit_planet_config_.wire_overlay);
        ImGui::Checkbox("Patch Skirts", &edit_planet_config_.skirts_enabled);
        ImGui::InputFloat("Skirt Depth Scale", &edit_planet_config_.skirt_depth_scale, 0.0F, 0.0F,
                          "%.2f");
        ImGui::Checkbox("Terrain", &edit_planet_config_.terrain_enabled);
        ImGui::InputFloat("Terrain Height (m)", &edit_planet_config_.terrain_height_scale_m, 0.0F,
                          0.0F, "%.0f");
        ImGui::InputFloat("Terrain Noise Scale", &edit_planet_config_.terrain_noise_scale, 0.0F,
                          0.0F, "%.2f");
        ImGui::InputFloat("Terrain Mid Detail", &edit_planet_config_.terrain_mid_detail_strength,
                          0.0F, 0.0F, "%.2f");
        ImGui::InputFloat("Terrain Fine Detail", &edit_planet_config_.terrain_fine_detail_strength,
                          0.0F, 0.0F, "%.2f");
        ImGui::InputFloat("Terrain Fine Scale", &edit_planet_config_.terrain_fine_detail_scale,
                          0.0F, 0.0F, "%.2f");
        int terrain_seed = static_cast<int>(edit_planet_config_.terrain_seed);
        if (ImGui::InputInt("Terrain Seed", &terrain_seed)) {
            edit_planet_config_.terrain_seed =
                static_cast<std::uint32_t>(std::max(terrain_seed, 0));
        }
        ImGui::InputFloat("Sea Level (m)", &edit_planet_config_.sea_level_m, 0.0F, 0.0F, "%.0f");
        ImGui::InputFloat("Bathymetry Depth (m)", &edit_planet_config_.bathymetry_depth_scale_m,
                          0.0F, 0.0F, "%.0f");
        ImGui::InputFloat("Shoreline Width (m)", &edit_planet_config_.shoreline_width_m, 0.0F, 0.0F,
                          "%.0f");

        if (edit_planet_config_ != config_before_edit) {
            planet_config_apply_pending_ = edit_planet_config_ != planet_config_;
        }
        if (ImGui::Button("Revert Config")) {
            edit_planet_config_ = planet_config_;
            planet_config_apply_pending_ = false;
            rebuild_error_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Camera")) {
            reset_camera();
        }
        maybe_apply_planet_config(context);
        if (!rebuild_error_.empty()) {
            ImGui::Text("Config error: %s", rebuild_error_.c_str());
        }

        ImGui::SeparatorText("Solar System");
        bool solar_changed = false;
        solar_changed |=
            ImGui::SliderFloat("Day", &solar_time_.day_of_year, 1.0F, 365.2422F, "%.1f");
        solar_changed |= ImGui::SliderFloat("Hour", &solar_time_.time_hours, 0.0F, 24.0F, "%.2f");
        solar_changed |=
            ImGui::SliderFloat("Speed (h/s)", &solar_time_.hours_per_second, -12.0F, 12.0F, "%.2f");
        if (solar_changed) {
            refresh_celestial_state();
        }
        const PlanetCelestialDiagnostics celestial_diagnostics =
            planet_celestial_diagnostics(solar_time_, solar_config_);
        constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
        ImGui::Text("Solar / sidereal day: %.2f h / %.4f h",
                    celestial_diagnostics.mean_solar_day_hours,
                    celestial_diagnostics.sidereal_rotation_hours);
        ImGui::Text("Year / lunar sidereal / synodic: %.4f d / %.6f d / %.5f d",
                    celestial_diagnostics.tropical_year_days,
                    celestial_diagnostics.lunar_sidereal_month_days,
                    celestial_diagnostics.lunar_synodic_month_days);
        ImGui::Text("Axial tilt / moon inclination: %.3f deg / %.3f deg",
                    celestial_diagnostics.axial_tilt_rad * kRadiansToDegrees,
                    celestial_diagnostics.lunar_orbit_inclination_rad * kRadiansToDegrees);
        ImGui::Text("Moon phase: %.3f", celestial_diagnostics.moon_phase_fraction);

        ImGui::SeparatorText("Diagnostics");
        ImGui::Text("Radius: %.0f m", planet_config_.radius_m);
        ImGui::Text("Atmosphere: %.0f m", planet_config_.atmosphere_height_m);
        ImGui::Text("Altitude: %.0f m", frame_.camera_altitude_m);
        ImGui::Text(
            "Surface camera: %.0f%%",
            planet_surface_camera_blend(planet_config_, planet_camera_distance_m(camera_state_)) *
                100.0F);
        ImGui::Text("Horizon: %.0f m", frame_.horizon_distance_m);
        ImGui::Text("Near / far: %.1f m / %.0f m", frame_.near_plane_m, frame_.far_plane_m);
        ImGui::Text("Origin: %.0f %.0f %.0f", frame_.surface_origin_m.x, frame_.surface_origin_m.y,
                    frame_.surface_origin_m.z);
        ImGui::Text("Solar time: %.2f h, day %.1f", solar_time_.time_hours,
                    solar_time_.day_of_year);
        ImGui::Text("Sun dir: %.2f %.2f %.2f", celestial_system_.sun.direction.x,
                    celestial_system_.sun.direction.y, celestial_system_.sun.direction.z);
        ImGui::Text("Moon dir: %.2f %.2f %.2f", celestial_system_.moon.direction.x,
                    celestial_system_.moon.direction.y, celestial_system_.moon.direction.z);
        ImGui::Text("Orbit angles: planet %.2f, rotation %.2f, moon %.2f",
                    celestial_system_.planet_orbit_angle_rad,
                    celestial_system_.planet_rotation_angle_rad,
                    celestial_system_.moon_orbit_angle_rad);

        ImGui::SeparatorText("Surface");
        ImGui::Text("Patches: %u rendered / %u planned",
                    surface_build_.diagnostics.visible_patch_count,
                    surface_build_.diagnostics.planned_patch_count);
        ImGui::Text("Patch budget: %u / %llu", surface_build_.diagnostics.patch_count,
                    static_cast<unsigned long long>(kPlanetMaxLivePatchInstances));
        ImGui::Text("Base / refined: %u / %u", surface_build_.diagnostics.base_patch_count,
                    surface_build_.diagnostics.refined_patch_count);
        ImGui::Text("Subdivided parents: %u", surface_build_.diagnostics.subdivided_patch_count);
        ImGui::Text("Fallback parents: %u",
                    surface_build_.diagnostics.refinement_fallback_patch_count);
        ImGui::Text("Budget fallback parents: %u",
                    surface_build_.diagnostics.budget_fallback_patch_count);
        ImGui::Text("Hysteresis delayed: %u splits / %u merges",
                    surface_build_.diagnostics.hysteresis_delayed_split_count,
                    surface_build_.diagnostics.hysteresis_delayed_merge_count);
        ImGui::Text("Refinement culled: %u horizon / %u view",
                    surface_build_.diagnostics.culled_horizon_count,
                    surface_build_.diagnostics.culled_view_count);
        ImGui::Text("LOD range: %u - %u", surface_build_.diagnostics.min_lod_level,
                    surface_build_.diagnostics.max_lod_level);
        for (std::size_t lod_index = 0;
             lod_index < surface_build_.diagnostics.patches_by_lod.size(); ++lod_index) {
            const std::uint32_t patch_count = surface_build_.diagnostics.patches_by_lod[lod_index];
            const float min_cell = surface_build_.diagnostics.min_cell_edge_m_by_lod[lod_index];
            const float max_cell = surface_build_.diagnostics.max_cell_edge_m_by_lod[lod_index];
            if (patch_count == 0U && lod_index > surface_build_.diagnostics.max_lod_level) {
                continue;
            }
            ImGui::Text("LOD %zu: %u patches, cell %.0f-%.0f m", lod_index, patch_count, min_cell,
                        max_cell);
        }
        ImGui::Text("Screen error: %.1f px - %.1f px",
                    surface_build_.diagnostics.min_screen_error_px,
                    surface_build_.diagnostics.max_screen_error_px);
        ImGui::Text("LOD transition: %u candidates, %.0f%% max pressure",
                    surface_build_.diagnostics.transition_candidate_count,
                    surface_build_.diagnostics.max_transition_pressure * 100.0F);
        ImGui::Text("LOD neighbors: %u edges, %u mismatched, max delta %u",
                    surface_build_.diagnostics.lod_neighbor_edge_count,
                    surface_build_.diagnostics.lod_neighbor_mismatch_edge_count,
                    surface_build_.diagnostics.max_lod_neighbor_delta);
        ImGui::Text("Surface vertices: %u", surface_build_.diagnostics.vertex_count);
        ImGui::Text("Surface triangles: %u", surface_build_.diagnostics.triangle_count);
        ImGui::Text("Cell edge: %.0f m - %.0f m", surface_build_.diagnostics.min_edge_length_m,
                    surface_build_.diagnostics.max_edge_length_m);
        ImGui::Text("Seam edges: %u", surface_build_.diagnostics.seam_edge_count);
        ImGui::Text("Skirt triangles: %u", surface_build_.diagnostics.skirt_triangle_count);
        ImGui::Text("Skirt depth: %.0f m - %.0f m", surface_build_.diagnostics.min_skirt_depth_m,
                    surface_build_.diagnostics.max_skirt_depth_m);

        cubey::host::draw_performance_ui({
            .frame_stats = latest_frame_stats_,
            .latest_fps = latest_fps_,
            .latest_frame_ms = latest_frame_ms_,
            .process = process_stats_.sample(),
            .device_memory_budget = context.device().device_memory_budget(),
            .config = {.default_open = false},
        });
    }

    [[nodiscard]] std::optional<cubey::host::FrameStatsSample>
    record_frame_stats(VkExtent2D extent, const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const cubey::host::FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = surface_build_.diagnostics.triangle_count,
        };
        if (std::optional<cubey::host::FrameStatsSnapshot> stats =
                ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void rebuild_planet_resources(cubey::host::WindowedAppContext& context) {
        validate_planet_config(edit_planet_config_);
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle planet topology rebuild");
        static_cast<void>(context.gpu().drain());
        patch_instance_buffers_.clear();
        patch_grid_mesh_.reset();
        previous_selected_patch_ids_.clear();

        planet_config_ = edit_planet_config_;
        refresh_camera_limits_for_planet();
        refresh_frame();
        rebuild_surface_data(context.swapchain().extent());
        patch_grid_mesh_.emplace(context.gpu(), patch_grid_.mesh_config());
        resize_patch_instance_buffer_slots(context.frame_slot_count());
        static_cast<void>(context.gpu().drain());
    }

    void apply_dynamic_planet_config(VkExtent2D extent) {
        validate_planet_config(edit_planet_config_);
        planet_config_ = edit_planet_config_;
        refresh_camera_limits_for_planet();
        refresh_frame();
        rebuild_surface_data(extent);
    }

    void maybe_apply_planet_config(cubey::host::WindowedAppContext& context) {
        if (!planet_config_apply_pending_ || ImGui::IsAnyItemActive()) {
            return;
        }
        const PlanetConfigChangeKind change_kind =
            planet_config_change_kind(planet_config_, edit_planet_config_);
        if (change_kind == PlanetConfigChangeKind::None) {
            planet_config_apply_pending_ = false;
            return;
        }
        try {
            if (change_kind == PlanetConfigChangeKind::SurfaceTopology) {
                rebuild_planet_resources(context);
            } else {
                apply_dynamic_planet_config(context.swapchain().extent());
            }
            rebuild_error_.clear();
            planet_config_apply_pending_ = false;
        } catch (const std::exception& error) {
            rebuild_error_ = error.what();
            planet_config_apply_pending_ = false;
        } catch (...) {
            rebuild_error_ = "unknown planet rebuild error";
            planet_config_apply_pending_ = false;
        }
    }

    void rebuild_surface_resources(VkExtent2D extent) {
        rebuild_surface_data(extent);
    }

    void refresh_camera_limits_for_planet() {
        planet_camera_set_distance(camera_state_, planet_config_,
                                   planet_camera_distance_m(camera_state_));
        planet_camera_update_surface_mode(camera_state_, planet_config_);
    }

    void reset_camera() {
        camera_state_ =
            planet_camera_home_state(planet_config_, kPlanetCameraBaseYaw, kPlanetCameraBasePitch);
    }

    [[nodiscard]] cubey::Transform3D camera_transform() const {
        return make_planet_camera_transform(planet_config_, camera_state_);
    }

    void refresh_frame() {
        frame_ = make_planet_frame(planet_config_, camera_state_.position_m);
        camera_.set_projection(std::numbers::pi_v<float> / 3.0F, frame_.near_plane_m,
                               frame_.far_plane_m);
    }

    [[nodiscard]] static VkExtent2D default_surface_extent() {
        return {1280U, 720U};
    }

    [[nodiscard]] PlanetSurfaceView surface_view(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        return {
            .camera_world_position_m = frame_.camera_world_position_m,
            .camera_forward_world =
                glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F}),
            .vertical_fov_radians = camera_.fovy_radians(),
            .aspect_ratio = aspect,
            .viewport_height_px = static_cast<float>(std::max(extent.height, 1U)),
            .culling_enabled = true,
        };
    }

    void refresh_render_diagnostics(const PlanetSurfacePatchPlan& plan) {
        surface_build_ = {};
        surface_build_.diagnostics = plan.diagnostics;
        surface_build_.diagnostics.vertex_count =
            static_cast<std::uint32_t>(patch_grid_.vertices.size() * patch_instances_.size());
        surface_build_.diagnostics.triangle_count =
            static_cast<std::uint32_t>((patch_grid_.indices.size() / 3U) * patch_instances_.size());

        for (std::size_t index = 0;
             index < surface_build_.diagnostics.min_cell_edge_m_by_lod.size(); ++index) {
            const float min_edge = surface_build_.diagnostics.min_cell_edge_m_by_lod[index];
            const float max_edge = surface_build_.diagnostics.max_cell_edge_m_by_lod[index];
            if (min_edge > 0.0F && (surface_build_.diagnostics.min_edge_length_m == 0.0F ||
                                    min_edge < surface_build_.diagnostics.min_edge_length_m)) {
                surface_build_.diagnostics.min_edge_length_m = min_edge;
            }
            surface_build_.diagnostics.max_edge_length_m =
                std::max(surface_build_.diagnostics.max_edge_length_m, max_edge);
        }

        if (planet_config_.skirts_enabled && surface_build_.diagnostics.patch_count > 0U) {
            surface_build_.diagnostics.seam_edge_count =
                surface_build_.diagnostics.patch_count * 4U;
            surface_build_.diagnostics.skirt_triangle_count =
                surface_build_.diagnostics.patch_count * planet_config_.patch_resolution * 16U;
            const float min_cell = surface_build_.diagnostics.min_edge_length_m > 0.0F
                                       ? surface_build_.diagnostics.min_edge_length_m
                                       : planet_config_.radius_m * 0.00001F;
            const float max_cell = std::max(surface_build_.diagnostics.max_edge_length_m, min_cell);
            surface_build_.diagnostics.min_skirt_depth_m = std::max(
                min_cell * planet_config_.skirt_depth_scale, planet_config_.radius_m * 0.00001F);
            surface_build_.diagnostics.max_skirt_depth_m = std::max(
                max_cell * planet_config_.skirt_depth_scale, planet_config_.radius_m * 0.00001F);
        }
    }

    void rebuild_surface_data(VkExtent2D extent) {
        const PlanetSurfaceView view = surface_view(extent);
        const PlanetSurfacePatchPlan plan = plan_planet_surface_patches(
            planet_config_, view,
            PlanetSurfacePatchSelectionHints{
                .previous_selected_patches = previous_selected_patch_ids_,
            });
        patch_grid_ = make_planet_patch_grid_mesh(planet_config_);
        patch_instances_ = make_planet_surface_gpu_patch_instances(planet_config_, plan);
        refresh_previous_selected_patch_ids(plan);
        mark_patch_instance_buffers_stale();
        refresh_render_diagnostics(plan);
        surface_build_render_origin_world_m_ = frame_.render_origin_world_m;
        surface_build_view_ = view;
    }

    void refresh_previous_selected_patch_ids(const PlanetSurfacePatchPlan& plan) {
        previous_selected_patch_ids_.clear();
        previous_selected_patch_ids_.reserve(plan.selected_patches.size());
        for (const PlanetSurfacePatchInstance& patch : plan.selected_patches) {
            previous_selected_patch_ids_.push_back(patch.id);
        }
    }

    [[nodiscard]] bool surface_plan_changed(VkExtent2D extent) const {
        const PlanetSurfaceView view = surface_view(extent);
        const double origin_threshold_m =
            std::max(static_cast<double>(planet_config_.radius_m) * 0.002, 256.0);
        if (glm::length(frame_.render_origin_world_m - surface_build_render_origin_world_m_) >
            origin_threshold_m) {
            return true;
        }
        if (std::abs(view.aspect_ratio - surface_build_view_.aspect_ratio) > 0.005F ||
            std::abs(view.viewport_height_px - surface_build_view_.viewport_height_px) > 1.0F) {
            return true;
        }
        const float forward_dot =
            glm::dot(glm::normalize(view.camera_forward_world),
                     glm::normalize(surface_build_view_.camera_forward_world));
        return forward_dot < 0.9985F;
    }

    [[nodiscard]] cubey::Transform3D camera_render_transform() const {
        cubey::Transform3D transform = camera_transform();
        const cubey::math::DVec3 relative_camera =
            frame_.camera_world_position_m - surface_build_render_origin_world_m_;
        transform.translation = {
            static_cast<float>(relative_camera.x),
            static_cast<float>(relative_camera.y),
            static_cast<float>(relative_camera.z),
        };
        return transform;
    }

    [[nodiscard]] PlanetSurfaceFrameUniforms surface_frame_uniforms(VkExtent2D extent) {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        refresh_frame();
        const cubey::Transform3D transform = camera_render_transform();
        const float terrain_height =
            planet_config_.terrain_enabled ? planet_config_.terrain_height_scale_m : 0.0F;
        const float skirt_depth = std::max(surface_build_.diagnostics.min_skirt_depth_m,
                                           planet_config_.radius_m * 0.00001F);
        const float ambient_intensity = surface_ambient_intensity();
        const float direct_intensity = surface_direct_intensity();
        const cubey::math::Vec3 haze_color = surface_haze_color();
        const PlanetCelestialDiagnostics celestial_diagnostics =
            planet_celestial_diagnostics(solar_time_, solar_config_);
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .light_direction_debug =
                {
                    celestial_lighting_.primary_light_direction.x,
                    celestial_lighting_.primary_light_direction.y,
                    celestial_lighting_.primary_light_direction.z,
                    planet_config_.lod_target_edge_px,
                },
            .render_origin_radius =
                {
                    static_cast<float>(surface_build_render_origin_world_m_.x),
                    static_cast<float>(surface_build_render_origin_world_m_.y),
                    static_cast<float>(surface_build_render_origin_world_m_.z),
                    planet_config_.radius_m,
                },
            .surface_options =
                {
                    packed_debug_wire_option(planet_config_),
                    packed_patch_lod_option(planet_config_),
                    packed_terrain_detail_strengths(planet_config_),
                    planet_config_.terrain_fine_detail_scale,
                },
            .terrain_options =
                {
                    terrain_height,
                    planet_config_.terrain_noise_scale,
                    static_cast<float>(planet_config_.terrain_seed),
                    skirt_depth,
                },
            .field_options =
                {
                    planet_config_.sea_level_m,
                    planet_config_.bathymetry_depth_scale_m,
                    planet_config_.shoreline_width_m,
                    0.0F,
                },
            .camera_horizon =
                {
                    transform.translation.x,
                    transform.translation.y,
                    transform.translation.z,
                    frame_.horizon_distance_m,
                },
            .atmosphere_options =
                {
                    ambient_intensity,
                    0.42F,
                    0.45F,
                    1.0F,
                },
            .haze_color_direct =
                {
                    haze_color.r,
                    haze_color.g,
                    haze_color.b,
                    direct_intensity,
                },
            .celestial_equator_plane =
                {
                    celestial_diagnostics.equator_plane_normal.x,
                    celestial_diagnostics.equator_plane_normal.y,
                    celestial_diagnostics.equator_plane_normal.z,
                    0.0F,
                },
            .celestial_ecliptic_plane =
                {
                    celestial_diagnostics.ecliptic_plane_normal.x,
                    celestial_diagnostics.ecliptic_plane_normal.y,
                    celestial_diagnostics.ecliptic_plane_normal.z,
                    0.0F,
                },
            .celestial_moon_orbit_plane =
                {
                    celestial_diagnostics.moon_orbit_plane_normal.x,
                    celestial_diagnostics.moon_orbit_plane_normal.y,
                    celestial_diagnostics.moon_orbit_plane_normal.z,
                    0.0F,
                },
            .celestial_sun_direction =
                {
                    celestial_diagnostics.sun_direction.x,
                    celestial_diagnostics.sun_direction.y,
                    celestial_diagnostics.sun_direction.z,
                    0.0F,
                },
            .celestial_moon_direction =
                {
                    celestial_diagnostics.moon_direction.x,
                    celestial_diagnostics.moon_direction.y,
                    celestial_diagnostics.moon_direction.z,
                    celestial_diagnostics.moon_phase_fraction,
                },
            .camera_world_radius =
                {
                    static_cast<float>(frame_.camera_world_position_m.x),
                    static_cast<float>(frame_.camera_world_position_m.y),
                    static_cast<float>(frame_.camera_world_position_m.z),
                    frame_.camera_radius_m,
                },
            .atmosphere_radius_mode =
                {
                    frame_.atmosphere_outer_radius_m,
                    std::max(frame_.atmosphere_outer_radius_m - frame_.planet_radius_m, 0.0F),
                    static_cast<float>(static_cast<std::uint32_t>(
                        planet_config_.atmosphere_mode)),
                    celestial_lighting_.primary_light_angular_radius_rad,
                },
            .sun_color_intensity =
                {
                    celestial_lighting_.primary_light_color.r,
                    celestial_lighting_.primary_light_color.g,
                    celestial_lighting_.primary_light_color.b,
                    celestial_lighting_.primary_light_intensity,
                },
        };
    }

    [[nodiscard]] float surface_direct_intensity() const {
        return std::clamp(celestial_lighting_.primary_light_intensity, 0.03F, 1.20F);
    }

    [[nodiscard]] float surface_ambient_intensity() const {
        return std::clamp(celestial_lighting_.ambient_intensity, 0.025F, 0.30F);
    }

    [[nodiscard]] cubey::math::Vec3 surface_haze_color() const {
        return celestial_lighting_.haze_color;
    }

    void refresh_celestial_state() {
        celestial_system_ = planet_celestial_system_from_solar_time(solar_time_, solar_config_);
        celestial_lighting_ = planet_celestial_lighting(celestial_system_);
    }

    void update_solar_time(double delta_seconds) {
        const PlanetSolarTime before = solar_time_;
        planet_solar_time_advance(solar_time_, delta_seconds);
        if (solar_time_.day_of_year != before.day_of_year ||
            solar_time_.time_hours != before.time_hours) {
            refresh_celestial_state();
        }
    }

    [[nodiscard]] PlanetSkyFrameUniforms sky_frame_uniforms(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        return planet_sky_frame_uniforms(
            celestial_system_, {
                                   .view_rays = cubey::render::view_ray_basis_3d(
                                       transform.rotation, aspect, camera_.fovy_radians()),
                                   .camera_position_m = transform.translation,
                                   .planet_radius_m = planet_config_.radius_m,
                                   .atmosphere_outer_radius_m =
                                       planet_config_.radius_m + planet_config_.atmosphere_height_m,
                                   .atmosphere_mode = planet_config_.atmosphere_mode,
                               });
    }

    void record_sky_frame(const cubey::vulkan::CommandRecorder& recorder,
                          cubey::render::ColorTargetView target,
                          cubey::render::FrameSlot frame_slot) const {
        sky_frame_.record_pass(recorder, target, frame_slot);
    }

    [[nodiscard]] PlanetCelestialBodyFrameUniforms
    moon_body_frame_uniforms(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::Transform3D transform = camera_render_transform();
        const PlanetCelestialBody moon = planet_celestial_moon_body(celestial_system_);
        const PlanetCelestialBodyRenderPlacement placement = planet_celestial_body_render_placement(
            moon, {
                      .camera_render_position_m = transform.translation,
                      .camera_world_position_m = frame_.camera_world_position_m,
                      .planet_center_world_position_m = {0.0, 0.0, 0.0},
                      .near_plane_m = frame_.near_plane_m,
                      .far_plane_m = frame_.far_plane_m,
                      .angular_radius_scale = kPlanetMoonAngularRadiusScale,
                      .shell_distance_fraction = kPlanetMoonShellDistanceFraction,
                  });
        return planet_celestial_body_frame_uniforms(
            moon, placement, celestial_lighting_, camera_.view_projection_matrix(transform, aspect),
            {
                .camera_render_position_m = transform.translation,
                .atmosphere =
                    {
                        .camera_position_m =
                            {
                                static_cast<float>(frame_.camera_world_position_m.x),
                                static_cast<float>(frame_.camera_world_position_m.y),
                                static_cast<float>(frame_.camera_world_position_m.z),
                            },
                        .planet_radius_m = planet_config_.radius_m,
                        .atmosphere_outer_radius_m =
                            planet_config_.radius_m + planet_config_.atmosphere_height_m,
                    },
            });
    }

    void record_moon_body_frame(const cubey::vulkan::CommandRecorder& recorder,
                                const cubey::render::RenderTargetView& target,
                                cubey::render::FrameSlot frame_slot) const {
        celestial_body_frame_.record_pass(recorder, target, frame_slot, moon_mesh());
    }

    [[nodiscard]] float display_exposure() const {
        return config_.pbr.exposure;
    }

    [[nodiscard]] cubey::render::PbrPostUniforms post_uniforms(VkFormat color_format) const {
        return cubey::render::hdr_post_uniforms(color_format, display_exposure());
    }

    void record_post_pass(const cubey::vulkan::CommandRecorder& recorder,
                          cubey::render::ColorTargetView target,
                          cubey::render::FrameSlot frame_slot) const {
        hdr_post_frame_.record_pass(recorder, target, frame_slot);
    }

    template <typename RecordCallback>
    void record_planet_surface_pass(const cubey::vulkan::CommandRecorder& recorder,
                                    const cubey::render::RenderTargetView& target,
                                    RecordCallback&& record_callback) const {
        const cubey::render::RenderTargetRenderingInfo rendering(
            target, forward_pass().clear_values(),
            cubey::render::RenderTargetAttachmentOps{
                .color = cubey::vulkan::load_store_attachment_ops(),
                .depth = cubey::vulkan::clear_store_attachment_ops(),
            });
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(target.color.extent);
        std::forward<RecordCallback>(record_callback)(recorder);
        recorder.end_rendering();
    }

    [[nodiscard]] PlanetFrameGraph build_frame_graph(
        cubey::render::ColorTargetView color_target, cubey::render::FrameSlot frame_slot,
        bool present,
        const cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>& instance_buffer) {
        const VkDescriptorSet frame_set = surface_frame_material().set(frame_slot);
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureState initial_state =
            present ? cubey::render::render_graph_undefined_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureState final_state =
            present ? cubey::render::render_graph_present_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "planet backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "planet scene color", color_target.extent, kPlanetSceneColorFormat));
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("planet depth", forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());

        graph.add_pass("planet sky", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .material_pass(planet_sky_pass_info())
            .execute([this, scene_color,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_sky_frame(context.recorder(),
                                 cubey::render::resolved_color_target_view(context, scene_color),
                                 frame_slot);
            });
        graph.add_pass("planet surface", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(planet_pass_info())
            .execute([this, scene_color, depth, frame_set,
                      &instance_buffer](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView resolved_color =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView resolved_depth =
                    cubey::render::resolved_depth_target_view(context, depth);
                record_planet_surface_pass(
                    context.recorder(),
                    cubey::render::render_target_view(resolved_color, resolved_depth),
                    [this, frame_set,
                     &instance_buffer](const cubey::vulkan::CommandRecorder& pass_recorder) {
                        pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                    forward_pass().pipeline().pipeline());
                        pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                          forward_pass().pipeline().layout(), 0,
                                                          frame_set);
                        instance_buffer.bind(pass_recorder, 1);
                        cubey::render::record_draw_item(
                            pass_recorder.handle(), cubey::render::DrawItem{
                                                        .mesh = &patch_grid_mesh(),
                                                        .instance_count = instance_buffer.count(),
                                                    });
                    });
            });
        graph.add_pass("planet moon", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(planet_celestial_body_pass_info())
            .execute([this, scene_color, depth,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView resolved_color =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView resolved_depth =
                    cubey::render::resolved_depth_target_view(context, depth);
                record_moon_body_frame(
                    context.recorder(),
                    cubey::render::render_target_view(resolved_color, resolved_depth), frame_slot);
            });
        graph.add_pass("planet post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, color_target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_post_pass(context.recorder(), color_target, frame_slot);
            });

        return {
            .graph = graph.compile(),
            .scene_color = scene_color,
        };
    }

    void update_post_descriptor(const cubey::vulkan::Device& device,
                                cubey::render::FrameSlot frame_slot,
                                const cubey::render::CompiledRenderGraph& graph,
                                const cubey::render::RenderGraphResourceSet& resources,
                                cubey::render::RenderGraphTextureHandle scene_color) const {
        hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, graph, resources,
                                                      scene_color);
    }

    void record_planet_frame(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                             VkCommandBuffer command_buffer,
                             cubey::render::ColorTargetView color_target,
                             cubey::render::FrameSlot frame_slot, bool present) {
        const PlanetSurfaceFrameUniforms uniforms = surface_frame_uniforms(color_target.extent);
        surface_frame_material().upload(frame_slot, uniforms);
        sky_frame_.upload(frame_slot, sky_frame_uniforms(color_target.extent));
        celestial_body_frame_.upload(frame_slot, moon_body_frame_uniforms(color_target.extent));
        hdr_post_frame_.upload(frame_slot, post_uniforms(color_target.format));
        const cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>& instance_buffer =
            ensure_patch_instance_buffer(gpu, frame_slot);
        const PlanetFrameGraph frame_graph =
            build_frame_graph(color_target, frame_slot, present, instance_buffer);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer planet",
                .command_buffer_mode =
                    present ? cubey::render::RenderGraphCommandBufferMode::BeginAndEnd
                            : cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            frame_graph.graph,
            [this, &device, frame_slot,
             &frame_graph](const cubey::render::RenderGraphResourceSet& resources) {
                update_post_descriptor(device, frame_slot, frame_graph.graph, resources,
                                       frame_graph.scene_color);
            });
    }

    [[nodiscard]] const cubey::render::Mesh& patch_grid_mesh() const {
        if (!patch_grid_mesh_.has_value()) {
            throw std::runtime_error("planet patch grid mesh is not initialized");
        }
        return patch_grid_mesh_.value();
    }

    [[nodiscard]] const cubey::render::Mesh& moon_mesh() const {
        if (!moon_mesh_.has_value()) {
            throw std::runtime_error("planet moon mesh is not initialized");
        }
        return moon_mesh_.value();
    }

    void resize_patch_instance_buffer_slots(std::uint32_t frame_slot_count) {
        if (frame_slot_count == 0U) {
            throw std::runtime_error(
                "planet patch instance buffers require at least one frame slot");
        }
        if (patch_instance_buffers_.size() == frame_slot_count) {
            return;
        }
        patch_instance_buffers_.clear();
        patch_instance_buffers_.resize(frame_slot_count);
    }

    void mark_patch_instance_buffers_stale() {
        ++patch_instance_generation_;
        if (patch_instance_generation_ != 0U) {
            return;
        }
        patch_instance_generation_ = 1U;
        for (PatchInstanceBufferSlot& slot : patch_instance_buffers_) {
            slot.generation = 0U;
        }
    }

    [[nodiscard]] const cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>&
    ensure_patch_instance_buffer(cubey::vulkan::GpuRuntime& gpu,
                                 cubey::render::FrameSlot frame_slot) {
        cubey::render::validate_frame_slot(frame_slot);
        if (frame_slot.count != patch_instance_buffers_.size()) {
            throw std::runtime_error("planet patch instance buffer frame slot count mismatch");
        }
        if (patch_instances_.empty()) {
            throw std::runtime_error("planet patch instance upload requires non-empty patches");
        }
        PatchInstanceBufferSlot& slot = patch_instance_buffers_.at(frame_slot.index);
        if (!slot.buffer.has_value() || slot.generation != patch_instance_generation_) {
            slot.buffer.emplace(gpu,
                                std::span<const PlanetSurfaceGpuPatchInstance>{patch_instances_});
            slot.generation = patch_instance_generation_;
        }
        return slot.buffer.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("planet forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    void create_sky_frame_resources_if_needed(const cubey::vulkan::Device& device,
                                              std::uint32_t frame_slot_count) {
        if (!sky_frame_.materials_created() ||
            sky_frame_.material().material_instance().set_count() != frame_slot_count) {
            sky_frame_.destroy();
            sky_frame_.create_materials(device, {
                                                    .frame_slot_count = frame_slot_count,
                                                });
        }
    }

    void create_sky_frame_pipeline(const cubey::vulkan::Device& device, VkExtent2D extent,
                                   VkFormat color_format) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("planet_sky.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("planet_sky.frag.spv")),
        };
        sky_frame_.destroy_pipeline();
        sky_frame_.create_pipeline(device, {
                                               .extent = extent,
                                               .color_format = color_format,
                                               .shader_stage_files = shaders,
                                           });
    }

    void create_celestial_body_frame_resources_if_needed(const cubey::vulkan::Device& device,
                                                         std::uint32_t frame_slot_count) {
        if (!celestial_body_frame_.materials_created() ||
            celestial_body_frame_.material().material_instance().set_count() != frame_slot_count) {
            celestial_body_frame_.destroy();
            celestial_body_frame_.create_materials(device, {
                                                               .frame_slot_count = frame_slot_count,
                                                           });
        }
    }

    void create_celestial_body_frame_pipeline(const cubey::vulkan::Device& device,
                                              VkExtent2D extent, VkFormat color_format,
                                              VkFormat depth_format) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("planet_celestial_body.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("planet_celestial_body.frag.spv")),
        };
        celestial_body_frame_.destroy_pipeline();
        celestial_body_frame_.create_pipeline(device, {
                                                          .extent = extent,
                                                          .color_format = color_format,
                                                          .depth_format = depth_format,
                                                          .shader_stage_files = shaders,
                                                      });
    }

    void create_hdr_post_resources_if_needed(const cubey::vulkan::Device& device,
                                             std::uint32_t frame_slot_count) {
        if (hdr_post_frame_slot_count_ == frame_slot_count) {
            return;
        }
        hdr_post_frame_.destroy();
        hdr_post_frame_.create_materials(device, {
                                                     .frame_slot_count = frame_slot_count,
                                                 });
        hdr_post_frame_slot_count_ = frame_slot_count;
    }

    void create_hdr_post_pipeline(const cubey::vulkan::Device& device, VkExtent2D extent,
                                  VkFormat color_format) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("forward_pbr_post.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("forward_pbr_post.frag.spv")),
        };
        hdr_post_frame_.destroy_pipeline();
        hdr_post_frame_.create_pipeline(device, {
                                                    .extent = extent,
                                                    .color_format = color_format,
                                                    .shader_stage_files = shaders,
                                                });
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<PlanetSurfaceFrameUniforms>&
    surface_frame_material() const {
        if (!surface_frame_material_.has_value()) {
            throw std::runtime_error("planet surface frame material is not initialized");
        }
        return surface_frame_material_.value();
    }

    RunConfig config_;
    PlanetConfig planet_config_{};
    PlanetConfig edit_planet_config_{};
    PlanetSolarTime solar_time_{};
    PlanetSolarSystemConfig solar_config_{};
    PlanetCelestialSystem celestial_system_{};
    PlanetCelestialLighting celestial_lighting_{};
    PlanetCameraState camera_state_{};
    cubey::Camera3D camera_{cubey::Camera3DConfig{.near_z = 1.0F, .far_z = 1500000.0F}};
    PlanetFrame frame_{};
    PlanetSurfaceBuildResult surface_build_{};
    PlanetPatchGridMeshData patch_grid_{};
    std::vector<PlanetSurfaceGpuPatchInstance> patch_instances_{};
    std::vector<PlanetSurfacePatchId> previous_selected_patch_ids_{};
    cubey::math::DVec3 surface_build_render_origin_world_m_{0.0, 0.0, 0.0};
    PlanetSurfaceView surface_build_view_{};
    bool surface_rebuild_pending_ = false;
    bool camera_interacting_ = false;
    bool planet_config_apply_pending_ = false;
    std::optional<cubey::render::Mesh> patch_grid_mesh_;
    std::optional<cubey::render::Mesh> moon_mesh_;
    std::vector<PatchInstanceBufferSlot> patch_instance_buffers_{};
    std::uint64_t patch_instance_generation_ = 0;
    std::optional<cubey::render::FrameUniformMaterialInstance<PlanetSurfaceFrameUniforms>>
        surface_frame_material_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
    PlanetSkyFrame sky_frame_{};
    PlanetCelestialBodyFrame celestial_body_frame_{};
    cubey::render::HdrPostFrame hdr_post_frame_{};
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<cubey::host::FrameStatsSnapshot> latest_frame_stats_;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    std::string rebuild_error_{};
    std::uint32_t hdr_post_frame_slot_count_ = 0;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
};

} // namespace

int run_planet(const RunConfig& config) {
    try {
        PlanetApp app(config);
        return app.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet: %s\n", error.what());
        return 1;
    }
}

} // namespace cubey::projects::planet
