#include "planet_app.h"

#include "planet_config.h"
#include "planet_frame.h"
#include "planet_surface.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/math.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/performance_ui.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/vk_check.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#ifndef CUBEY_PLANET_SHADER_DIR
#error "CUBEY_PLANET_SHADER_DIR must be defined by the planet target"
#endif

namespace cubey::projects::planet {
namespace {

constexpr const char* kAppName = "planet";
constexpr const char* kReadyStatus = "rendering planet project";

struct PlanetPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_debug{0.35F, 0.78F, 0.50F, 0.0F};
    cubey::math::Vec4 options{0.0F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(PlanetPushConstants) <= 128U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PLANET_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo planet_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PlanetPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "planet.forward",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] float planet_home_camera_distance(const PlanetConfig& config) {
    return std::max(config.radius_m + config.camera_altitude_m, config.radius_m * 1.01F);
}

[[nodiscard]] cubey::OrbitControllerConfig planet_orbit_config(const PlanetConfig& config) {
    const float home_distance = planet_home_camera_distance(config);
    return {
        .distance = home_distance,
        .min_distance = config.radius_m * 1.001F,
        .max_distance = std::max(home_distance * 8.0F, config.radius_m * 3.0F),
    };
}

class PlanetApp {
  public:
    explicit PlanetApp(RunConfig config)
        : config_(std::move(config)), planet_config_(planet_config_from_run_config(config_)),
          edit_planet_config_(planet_config_),
          orbit_controller_(planet_orbit_config(planet_config_)) {
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
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.gpu());
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
            record_planet_frame(context.device(), frame.command_buffer, frame.color_target,
                                frame.frame_slot, true);
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
            create_global_resources_if_needed(context.gpu());
            create_forward_pass(context.device(), context.render_target().extent,
                                context.render_target().format,
                                cubey::host::headless_capture_frame_slot_count(config_));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            record_planet_frame(context.device(), command_buffer, target, frame.frame_slot, false);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void create_global_resources_if_needed(cubey::vulkan::GpuRuntime& gpu) {
        if (surface_mesh_.has_value()) {
            return;
        }
        surface_mesh_.emplace(gpu, surface_build_.mesh.mesh_config());
    }

    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format, std::uint32_t frame_slot_count) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("planet_surface.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("planet_surface.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_uv_input_layout();
        forward_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{
                .extent = extent,
                .color_format = color_format,
            },
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .material_pass = planet_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.018F, 0.030F, 0.052F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        forward_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        surface_mesh_.reset();
    }

    void update_input(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
        refresh_frame();
        if (surface_mesh_.has_value() && surface_plan_changed(context.swapchain().extent())) {
            surface_rebuild_pending_ = true;
        }
        if (surface_mesh_.has_value() && surface_rebuild_pending_ &&
            !orbit_controller_.dragging()) {
            rebuild_surface_resources(context, context.swapchain().extent());
            surface_rebuild_pending_ = false;
        }
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
        if (ImGui::InputInt("Patch Resolution", &patch_resolution)) {
            edit_planet_config_.patch_resolution =
                static_cast<std::uint32_t>(std::max(patch_resolution, 0));
        }
        int max_lod_level = static_cast<int>(edit_planet_config_.max_lod_level);
        if (ImGui::InputInt("Max LOD Level", &max_lod_level)) {
            edit_planet_config_.max_lod_level =
                static_cast<std::uint32_t>(std::max(max_lod_level, 0));
        }
        ImGui::InputFloat("LOD Target Edge (px)", &edit_planet_config_.lod_target_edge_px, 0.0F,
                          0.0F, "%.1f");
        constexpr const char* kDebugViews[]{"final",     "face-id",      "patch-id",
                                            "lod-level", "screen-error", "seams"};
        int debug_view = static_cast<int>(edit_planet_config_.debug_view);
        if (ImGui::Combo("Debug View", &debug_view, kDebugViews,
                         static_cast<int>(std::size(kDebugViews)))) {
            edit_planet_config_.debug_view = static_cast<PlanetDebugView>(debug_view);
        }
        ImGui::Checkbox("Wire Overlay", &edit_planet_config_.wire_overlay);
        ImGui::Checkbox("Patch Skirts", &edit_planet_config_.skirts_enabled);
        ImGui::InputFloat("Skirt Depth Scale", &edit_planet_config_.skirt_depth_scale, 0.0F, 0.0F,
                          "%.2f");

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
            orbit_controller_.reset();
        }
        maybe_apply_planet_config(context);
        if (!rebuild_error_.empty()) {
            ImGui::Text("Config error: %s", rebuild_error_.c_str());
        }

        ImGui::SeparatorText("Diagnostics");
        ImGui::Text("Radius: %.0f m", planet_config_.radius_m);
        ImGui::Text("Atmosphere: %.0f m", planet_config_.atmosphere_height_m);
        ImGui::Text("Altitude: %.0f m", frame_.camera_altitude_m);
        ImGui::Text("Horizon: %.0f m", frame_.horizon_distance_m);
        ImGui::Text("Near / far: %.1f m / %.0f m", frame_.near_plane_m, frame_.far_plane_m);
        ImGui::Text("Origin: %.0f %.0f %.0f", frame_.surface_origin_m.x, frame_.surface_origin_m.y,
                    frame_.surface_origin_m.z);

        ImGui::SeparatorText("Surface");
        ImGui::Text("Patches: %u rendered / %u planned",
                    surface_build_.diagnostics.visible_patch_count,
                    surface_build_.diagnostics.planned_patch_count);
        ImGui::Text("Base / refined: %u / %u", surface_build_.diagnostics.base_patch_count,
                    surface_build_.diagnostics.refined_patch_count);
        ImGui::Text("Subdivided parents: %u", surface_build_.diagnostics.subdivided_patch_count);
        ImGui::Text("Fallback parents: %u",
                    surface_build_.diagnostics.refinement_fallback_patch_count);
        ImGui::Text("Refinement culled: %u horizon / %u view",
                    surface_build_.diagnostics.culled_horizon_count,
                    surface_build_.diagnostics.culled_view_count);
        ImGui::Text("LOD range: %u - %u", surface_build_.diagnostics.min_lod_level,
                    surface_build_.diagnostics.max_lod_level);
        ImGui::Text("LOD patches: %u / %u / %u / %u", surface_build_.diagnostics.patches_by_lod[0],
                    surface_build_.diagnostics.patches_by_lod[1],
                    surface_build_.diagnostics.patches_by_lod[2],
                    surface_build_.diagnostics.patches_by_lod[3]);
        ImGui::Text("Screen error: %.1f px - %.1f px",
                    surface_build_.diagnostics.min_screen_error_px,
                    surface_build_.diagnostics.max_screen_error_px);
        ImGui::Text("Surface vertices: %u", surface_build_.diagnostics.vertex_count);
        ImGui::Text("Surface triangles: %u", surface_build_.diagnostics.triangle_count);
        ImGui::Text("Cell edge: %.0f m - %.0f m", surface_build_.diagnostics.min_edge_length_m,
                    surface_build_.diagnostics.max_edge_length_m);
        ImGui::Text("LOD cell 0/1: %.0f-%.0f / %.0f-%.0f m",
                    surface_build_.diagnostics.min_cell_edge_m_by_lod[0],
                    surface_build_.diagnostics.max_cell_edge_m_by_lod[0],
                    surface_build_.diagnostics.min_cell_edge_m_by_lod[1],
                    surface_build_.diagnostics.max_cell_edge_m_by_lod[1]);
        ImGui::Text("LOD cell 2/3: %.0f-%.0f / %.0f-%.0f m",
                    surface_build_.diagnostics.min_cell_edge_m_by_lod[2],
                    surface_build_.diagnostics.max_cell_edge_m_by_lod[2],
                    surface_build_.diagnostics.min_cell_edge_m_by_lod[3],
                    surface_build_.diagnostics.max_cell_edge_m_by_lod[3]);
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
                             "vkDeviceWaitIdle planet rebuild");
        static_cast<void>(context.gpu().drain());
        surface_mesh_.reset();

        planet_config_ = edit_planet_config_;
        refresh_camera_limits_for_planet();
        refresh_frame();
        rebuild_surface_data(context.swapchain().extent());
        surface_mesh_.emplace(context.gpu(), surface_build_.mesh.mesh_config());
        static_cast<void>(context.gpu().drain());
    }

    void maybe_apply_planet_config(cubey::host::WindowedAppContext& context) {
        if (!planet_config_apply_pending_ || ImGui::IsAnyItemActive()) {
            return;
        }
        if (edit_planet_config_ == planet_config_) {
            planet_config_apply_pending_ = false;
            return;
        }
        try {
            rebuild_planet_resources(context);
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

    void rebuild_surface_resources(cubey::host::WindowedAppContext& context, VkExtent2D extent) {
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle planet surface rebuild");
        static_cast<void>(context.gpu().drain());
        surface_mesh_.reset();
        rebuild_surface_data(extent);
        surface_mesh_.emplace(context.gpu(), surface_build_.mesh.mesh_config());
        static_cast<void>(context.gpu().drain());
    }

    void refresh_camera_limits_for_planet() {
        const float current_distance = orbit_controller_.distance();
        const cubey::OrbitControllerConfig orbit_config = planet_orbit_config(planet_config_);
        orbit_controller_.set_distance_limits(orbit_config.min_distance, orbit_config.max_distance);
        orbit_controller_.set_home_distance(orbit_config.distance);
        orbit_controller_.set_distance(current_distance);
    }

    [[nodiscard]] cubey::Transform3D camera_transform() const {
        return cubey::orbit_camera_transform({
            .target = {0.0F, 0.0F, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw() + 0.55F,
            .pitch = orbit_controller_.pitch() + 0.28F,
        });
    }

    void refresh_frame() {
        frame_ = make_planet_frame(planet_config_, camera_transform());
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

    void rebuild_surface_data(VkExtent2D extent) {
        const PlanetSurfaceView view = surface_view(extent);
        const PlanetSurfacePatchPlan plan = plan_planet_surface_patches(planet_config_, view);
        surface_build_ = make_planet_surface_mesh(planet_config_, view, frame_, plan);
        surface_build_render_origin_world_m_ = frame_.render_origin_world_m;
        surface_build_view_ = view;
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

    [[nodiscard]] PlanetPushConstants push_constants(VkExtent2D extent) {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        refresh_frame();
        const cubey::Transform3D transform = camera_render_transform();
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .light_direction_debug = {0.35F, 0.78F, 0.50F, 0.0F},
            .options = {planet_config_.wire_overlay ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F},
        };
    }

    void record_planet_frame(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                             cubey::render::ColorTargetView color_target,
                             cubey::render::FrameSlot frame_slot, bool present) {
        const PlanetPushConstants constants = push_constants(color_target.extent);
        const auto record = [this,
                             &constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        forward_pass().pipeline().pipeline());
            pass_recorder.push_constants(forward_pass().pipeline().layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, constants);
            cubey::render::record_draw_item(pass_recorder.handle(),
                                            cubey::render::DrawItem{.mesh = &surface_mesh()});
        };

        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureState initial_state =
            present ? cubey::render::render_graph_undefined_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureState final_state =
            present ? cubey::render::render_graph_present_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "planet backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("planet depth", forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());

        graph.add_pass("planet surface", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(backbuffer)
            .write_depth(depth)
            .material_pass(planet_pass_info())
            .execute([this, backbuffer, depth,
                      record](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView resolved_color =
                    cubey::render::resolved_color_target_view(context, backbuffer);
                const cubey::render::DepthTargetView resolved_depth =
                    cubey::render::resolved_depth_target_view(context, depth);
                cubey::render::record_render_target_pass(
                    context.recorder(),
                    cubey::render::render_target_view(resolved_color, resolved_depth),
                    forward_pass().clear_values(), record);
            });

        const cubey::render::CompiledRenderGraph frame_graph = graph.compile();
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
            frame_graph);
    }

    [[nodiscard]] const cubey::render::Mesh& surface_mesh() const {
        if (!surface_mesh_.has_value()) {
            throw std::runtime_error("planet surface mesh is not initialized");
        }
        return surface_mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("planet forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    PlanetConfig planet_config_{};
    PlanetConfig edit_planet_config_{};
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_{cubey::Camera3DConfig{.near_z = 1.0F, .far_z = 1500000.0F}};
    PlanetFrame frame_{};
    PlanetSurfaceBuildResult surface_build_{};
    cubey::math::DVec3 surface_build_render_origin_world_m_{0.0, 0.0, 0.0};
    PlanetSurfaceView surface_build_view_{};
    bool surface_rebuild_pending_ = false;
    bool planet_config_apply_pending_ = false;
    std::optional<cubey::render::Mesh> surface_mesh_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<cubey::host::FrameStatsSnapshot> latest_frame_stats_;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    std::string rebuild_error_{};
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
