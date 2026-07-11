#include "terrain_app.h"

#include "terrain_clipmap.h"
#include "terrain_config.h"
#include "terrain_source_gpu.h"
#include "terrain_surface_controller.h"

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/imgui_helpers.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_TERRAIN_SHADER_DIR
#error "CUBEY_TERRAIN_SHADER_DIR must be defined by the terrain CMake target"
#endif

namespace cubey::projects::terrain {
namespace {

constexpr VkFormat kTerrainSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr float kTerrainHeadlessOrbitSpeed = 0.18F;

struct TerrainCameraFrame {
    float pitch_radians = -0.58F;
    float yaw_radians = 0.64F;
    float distance_scale = 0.72F;
    float target_height_fraction = 0.32F;
};

struct TerrainPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 camera_position_vertical_scale{0.0F, 0.0F, 0.0F, 1.0F};
    cubey::math::Vec4 light_direction_intensity{0.38F, 0.82F, 0.42F, 1.0F};
    cubey::math::Vec4 light_color_debug_view{1.0F, 0.94F, 0.82F, 0.0F};
    cubey::math::Vec4 ambient_color_outer_extent{0.16F, 0.19F, 0.23F, 16'384.0F};
};

static_assert(sizeof(TerrainPushConstants) ==
              sizeof(cubey::math::Mat4) + 4U * sizeof(cubey::math::Vec4));
static_assert(sizeof(TerrainPushConstants) <= 128U);

struct CompiledTerrainGraph {
    cubey::render::CompiledRenderGraph graph{};
    cubey::render::RenderGraphTextureHandle scene_color{};
};

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TERRAIN_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TerrainPushConstants),
    };
    return {
        .label = "terrain.forward",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] TerrainCameraFrame terrain_camera_frame(TerrainCameraPreset preset) {
    switch (preset) {
    case TerrainCameraPreset::Oblique:
        return {};
    case TerrainCameraPreset::Profile:
        return {
            .pitch_radians = -0.20F,
            .yaw_radians = 0.92F,
            .distance_scale = 0.66F,
            .target_height_fraction = 0.28F,
        };
    case TerrainCameraPreset::Top:
        return {
            .pitch_radians = -1.46F,
            .yaw_radians = 0.20F,
            .distance_scale = 0.92F,
            .target_height_fraction = 0.34F,
        };
    case TerrainCameraPreset::Surface:
    case TerrainCameraPreset::SurfaceLow:
        return {};
    }
    return {};
}

[[nodiscard]] bool terrain_surface_camera(TerrainCameraPreset preset) {
    return preset == TerrainCameraPreset::Surface || preset == TerrainCameraPreset::SurfaceLow;
}

[[nodiscard]] TerrainSourceSummary
terrain_scene_summary(const TerrainSourceParameters& source,
                      const cubey::render::ClipmapGrid2DConfig& clipmap) {
    TerrainSourceParameters clean_source = source;
    clean_source.weathering = TerrainWeatheringMode::Off;
    return summarize_terrain_source(clean_source, {0.0F, 0.0F}, clipmap.outer_half_extent * 1.5F,
                                    33U);
}

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
terrain_atmosphere_state(const RunConfig& config) {
    RunConfig::AtmosphereOptions atmosphere = config.atmosphere;
    const bool explicit_clock = cubey::run_config_float_is_set(atmosphere.time_hours) ||
                                cubey::run_config_float_is_set(atmosphere.day_of_year) ||
                                cubey::run_config_float_is_set(atmosphere.latitude_degrees);
    const bool explicit_sun = cubey::run_config_float_is_set(atmosphere.sun_elevation_degrees) ||
                              cubey::run_config_float_is_set(atmosphere.sun_azimuth_degrees);
    if (atmosphere.time_of_day_mode.empty() && !explicit_clock && !explicit_sun) {
        atmosphere.time_of_day_mode = "manual";
    }
    return cubey::atmosphere_environment_run_state_from_config(
        atmosphere,
        {
            .sun_elevation_degrees = 38.0F,
            .sun_azimuth_degrees = -42.0F,
            .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
            .reference_geometry_enabled = false,
        });
}

[[nodiscard]] float terrain_debug_view_id(TerrainDebugView view) {
    return static_cast<float>(static_cast<std::uint8_t>(view));
}

class TerrainApp {
  public:
    explicit TerrainApp(RunConfig config)
        : run_config_(std::move(config)),
          runtime_config_(terrain_runtime_config_from_run_config(run_config_)),
          source_parameters_(resolve_terrain_source_parameters(runtime_config_.source)),
          clipmap_data_(make_terrain_clipmap_mesh(runtime_config_)),
          clipmap_config_(terrain_clipmap_config(runtime_config_)),
          scene_summary_(terrain_scene_summary(source_parameters_, clipmap_config_)),
          orbit_controller_(cubey::OrbitControllerConfig{
              .distance = clipmap_config_.outer_half_extent *
                          terrain_camera_frame(runtime_config_.camera).distance_scale,
              .min_distance = 120.0F,
              .max_distance = clipmap_config_.outer_half_extent * 3.0F,
          }),
          camera_(cubey::Camera3DConfig{
              .near_z = 0.5F,
              .far_z = clipmap_config_.outer_half_extent * 5.0F,
          }),
          atmosphere_state_(terrain_atmosphere_state(run_config_)) {}

    TerrainApp(const TerrainApp&) = delete;
    TerrainApp& operator=(const TerrainApp&) = delete;

    int run() {
        return run_config_.headless ? run_headless() : run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_swapchain_resources(context.device(), context.swapchain().extent(),
                                       context.swapchain().format(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            update(context.filtered_input(), timing.delta_seconds);
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) { draw_ui(); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_target(context.device(), frame.command_buffer, frame.color_target,
                          frame.frame_slot, cubey::render::render_graph_undefined_texture_state(),
                          cubey::render::render_graph_present_texture_state(),
                          cubey::render::RenderGraphCommandBufferMode::BeginAndEnd);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
            return cubey::host::FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = context.swapchain().extent().width,
                .height = context.swapchain().extent().height,
                .triangles = terrain_clipmap_triangle_count(clipmap_data_),
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        return cubey::host::run_windowed_app(
            {
                .run_config = run_config_,
                .app_name = "terrain",
                .ready_status = "rendering terrain v1",
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
            const std::uint32_t frame_slot_count =
                cubey::host::headless_capture_frame_slot_count(run_config_);
            create_global_resources_if_needed(context.device(), context.gpu(), frame_slot_count);
            create_swapchain_resources(context.device(), context.render_target().extent,
                                       context.render_target().format, frame_slot_count);
        };
        if (run_config_.capture_mode == CaptureMode::Video) {
            orbit_controller_.set_auto_rotation_speed(kTerrainHeadlessOrbitSpeed);
            callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                            const cubey::host::HeadlessCaptureFrame& frame) {
                orbit_controller_.update(frame.timing.delta_seconds);
                (void)cubey::atmosphere_environment_advance_time(atmosphere_state_,
                                                                 frame.timing.delta_seconds);
            };
        }
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            record_target(context.device(), command_buffer, target, frame.frame_slot,
                          cubey::render::render_graph_color_attachment_texture_state(),
                          cubey::render::render_graph_color_attachment_texture_state(),
                          cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update(const cubey::input::FilteredInputFrame& input, double delta_seconds) {
        if (terrain_surface_camera(runtime_config_.camera)) {
            surface_controller_.update(input, delta_seconds);
        } else {
            orbit_controller_.update_from_input(input, delta_seconds);
        }
        (void)cubey::atmosphere_environment_advance_time(atmosphere_state_, delta_seconds);
    }

    void draw_ui() {
        if (!cubey::host::begin_control_panel("Terrain", {.width = 390.0F})) {
            ImGui::End();
            return;
        }

        bool source_changed = false;
        int preset = static_cast<int>(runtime_config_.source.preset);
        if (ImGui::Combo("Preset", &preset, "Mountain\0Upland\0Plains\0")) {
            runtime_config_.source.preset = static_cast<TerrainPreset>(preset);
            source_changed = true;
        }
        bool local_weathering = runtime_config_.source.weathering == TerrainWeatheringMode::Local;
        if (ImGui::Checkbox("Local weathering", &local_weathering)) {
            runtime_config_.source.weathering =
                local_weathering ? TerrainWeatheringMode::Local : TerrainWeatheringMode::Off;
            source_changed = true;
        }
        if (ImGui::SliderFloat("Weathering strength", &runtime_config_.source.weathering_strength,
                               0.0F, 1.0F, "%.2f")) {
            source_changed = true;
        }
        int camera = static_cast<int>(runtime_config_.camera);
        if (ImGui::Combo("Camera", &camera, "Oblique\0Profile\0Top\0Surface\0Surface low\0")) {
            runtime_config_.camera = static_cast<TerrainCameraPreset>(camera);
        }
        int debug_view = static_cast<int>(runtime_config_.debug_view);
        if (ImGui::Combo("View", &debug_view,
                         "Surface\0Height\0Base height\0Slope\0Weathering\0LOD\0")) {
            runtime_config_.debug_view = static_cast<TerrainDebugView>(debug_view);
        }
        if (source_changed) {
            refresh_source();
        }

        (void)cubey::host::draw_atmosphere_environment_controls(atmosphere_state_,
                                                                {.default_open = false});
        ImGui::End();
    }

    void refresh_source() {
        source_parameters_ = resolve_terrain_source_parameters(runtime_config_.source);
        scene_summary_ = terrain_scene_summary(source_parameters_, clipmap_config_);
    }

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (global_resources_created_) {
            return;
        }
        mesh_.emplace(gpu, clipmap_data_.mesh_config());
        source_material_.emplace(device, cubey::render::FrameUniformMaterialInstanceConfig{
                                             .material_pass = terrain_pass_info(),
                                             .descriptor_set = 0,
                                             .frame_slot_count = frame_slot_count,
                                             .uniform_binding = 0,
                                         });
        atmosphere_atlases_.emplace(cubey::render::create_atmosphere_background_generated_textures(
            device, gpu, {.night_sky_extent = 64U}));
        atmosphere_background_.create_materials(
            device,
            {.frame_slot_count = frame_slot_count, .textures = atmosphere_atlases_->bindings()});
        hdr_post_frame_.create_materials(device, {.frame_slot_count = frame_slot_count});
        global_resources_created_ = true;
    }

    void create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                    VkFormat color_format, std::uint32_t frame_slot_count) {
        const std::array terrain_shaders{
            cubey::render::vertex_shader_file(shader_path("terrain.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("terrain.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_input_layout();
        const std::array<VkDescriptorSetLayout, 1> terrain_descriptor_set_layouts{
            source_material().layout(),
        };
        terrain_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{
                .extent = extent,
                .color_format = kTerrainSceneColorFormat,
            },
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = terrain_shaders,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .descriptor_set_layouts = terrain_descriptor_set_layouts,
                        .material_pass = terrain_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
            });

        const std::array atmosphere_shaders{
            cubey::render::vertex_shader_file(shader_path("atmosphere.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("atmosphere.frag.spv")),
        };
        atmosphere_background_.create_pipeline(device, {.extent = extent,
                                                        .color_format = kTerrainSceneColorFormat,
                                                        .shader_stage_files = atmosphere_shaders});

        const std::array post_shaders{
            cubey::render::vertex_shader_file(shader_path("forward_pbr_post.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("forward_pbr_post.frag.spv")),
        };
        hdr_post_frame_.create_pipeline(
            device,
            {.extent = extent, .color_format = color_format, .shader_stage_files = post_shaders});
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        hdr_post_frame_.destroy_pipeline();
        atmosphere_background_.destroy_pipeline();
        terrain_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        hdr_post_frame_.destroy();
        atmosphere_background_.destroy();
        atmosphere_atlases_.reset();
        source_material_.reset();
        mesh_.reset();
        global_resources_created_ = false;
    }

    [[nodiscard]] cubey::Transform3D current_camera_transform() const {
        if (terrain_surface_camera(runtime_config_.camera)) {
            const float clearance =
                runtime_config_.camera == TerrainCameraPreset::SurfaceLow ? 18.0F : 70.0F;
            return surface_controller_.camera_transform(source_parameters_,
                                                        runtime_config_.vertical_scale, clearance);
        }
        const TerrainCameraFrame frame = terrain_camera_frame(runtime_config_.camera);
        const float target_y = (scene_summary_.min_height_m +
                                (scene_summary_.max_height_m - scene_summary_.min_height_m) *
                                    frame.target_height_fraction) *
                               runtime_config_.vertical_scale;
        return cubey::orbit_camera_transform({
            .target = {0.0F, target_y, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw() + frame.yaw_radians,
            .pitch = orbit_controller_.pitch() + frame.pitch_radians,
        });
    }

    [[nodiscard]] float aspect(VkExtent2D extent) const {
        return extent.height == 0U
                   ? 1.0F
                   : static_cast<float>(extent.width) / static_cast<float>(extent.height);
    }

    [[nodiscard]] TerrainPushConstants push_constants(VkExtent2D extent) const {
        const cubey::render::AtmosphereEnvironmentLighting lighting =
            cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment);
        const cubey::math::Vec3 ambient =
            lighting.ambient_color * std::max(lighting.ambient_intensity, 0.1F);
        return {
            .view_projection =
                camera_.view_projection_matrix(frame_camera_transform_, aspect(extent)),
            .camera_position_vertical_scale = {frame_camera_transform_.translation.x,
                                               frame_camera_transform_.translation.y,
                                               frame_camera_transform_.translation.z,
                                               runtime_config_.vertical_scale},
            .light_direction_intensity = {lighting.primary_light_direction.x,
                                          lighting.primary_light_direction.y,
                                          lighting.primary_light_direction.z,
                                          std::max(lighting.primary_light_intensity * 0.55F, 0.1F)},
            .light_color_debug_view = {lighting.primary_light_color.x,
                                       lighting.primary_light_color.y,
                                       lighting.primary_light_color.z,
                                       terrain_debug_view_id(runtime_config_.debug_view)},
            .ambient_color_outer_extent = {ambient.x * 0.65F, ambient.y * 0.65F, ambient.z * 0.65F,
                                           clipmap_config_.outer_half_extent},
        };
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_uniforms(VkExtent2D extent) const {
        return cubey::render::atmosphere_environment_frame_uniforms(
            atmosphere_state_.environment,
            {
                .view_rays = cubey::render::view_ray_basis_3d(
                    frame_camera_transform_.rotation, aspect(extent), camera_.fovy_radians()),
                .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
                .camera_position_km = {0.0F,
                                       atmosphere_state_.environment.bottom_radius_km +
                                           std::max(frame_camera_transform_.translation.y, 0.0F) *
                                               0.001F,
                                       0.0F},
                .camera_position_km_explicit = true,
            });
    }

    [[nodiscard]] float display_exposure() const {
        return run_config_.pbr.exposure_explicit ? run_config_.pbr.exposure
                                                 : atmosphere_state_.resolved_exposure;
    }

    void record_terrain_pass(const cubey::vulkan::CommandRecorder& recorder,
                             cubey::render::ColorTargetView color,
                             cubey::render::DepthTargetView depth,
                             cubey::render::FrameSlot frame_slot) const {
        const cubey::render::RenderTargetAttachmentOps attachment_ops{
            .color = cubey::vulkan::load_store_attachment_ops(),
            .depth = cubey::vulkan::clear_discard_attachment_ops(),
        };
        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(color, depth);
        const cubey::render::RenderTargetRenderingInfo rendering(
            target, terrain_forward_pass().clear_values(), attachment_ops);
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(color.extent);
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                               terrain_forward_pass().pipeline().pipeline());
        cubey::render::bind_material_instance(recorder, terrain_forward_pass().pipeline(),
                                              source_material().material(), frame_slot);
        recorder.push_constants(terrain_forward_pass().pipeline().layout(),
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                push_constants(color.extent));
        cubey::render::record_draw_item(recorder.handle(),
                                        cubey::render::DrawItem{.mesh = &mesh()});
        recorder.end_rendering();
    }

    [[nodiscard]] CompiledTerrainGraph
    current_render_graph(cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
                         cubey::render::RenderGraphTextureState target_initial_state,
                         cubey::render::RenderGraphTextureState target_final_state) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "terrain backbuffer", target, target_initial_state, target_final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "terrain scene color", target.extent, kTerrainSceneColorFormat));
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("terrain depth", terrain_forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());

        graph.add_pass("terrain atmosphere", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .material_pass(cubey::render::atmosphere_background_pass_info())
            .execute([this, scene_color,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                atmosphere_background_.record_pass(
                    context.recorder(),
                    cubey::render::resolved_color_target_view(context, scene_color), frame_slot);
            });
        graph.add_pass("terrain surface", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(terrain_pass_info())
            .execute([this, scene_color, depth,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_terrain_pass(context.recorder(),
                                    cubey::render::resolved_color_target_view(context, scene_color),
                                    cubey::render::resolved_depth_target_view(context, depth),
                                    frame_slot);
            });
        graph.add_pass("terrain post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                hdr_post_frame_.record_pass(context.recorder(), target, frame_slot);
            });
        return {.graph = graph.compile(), .scene_color = scene_color};
    }

    void record_target(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                       cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
                       cubey::render::RenderGraphTextureState target_initial_state,
                       cubey::render::RenderGraphTextureState target_final_state,
                       cubey::render::RenderGraphCommandBufferMode command_buffer_mode) {
        frame_camera_transform_ = current_camera_transform();
        source_material().upload(frame_slot, terrain_source_gpu_parameters(source_parameters_));
        atmosphere_background_.upload(frame_slot, atmosphere_uniforms(target.extent));
        hdr_post_frame_.upload(frame_slot,
                               cubey::render::hdr_post_uniforms(target.format, display_exposure()));

        const CompiledTerrainGraph render_graph =
            current_render_graph(target, frame_slot, target_initial_state, target_final_state);
        const auto prepare_resources = [this, &device, frame_slot, &render_graph](
                                           const cubey::render::RenderGraphResourceSet& resources) {
            hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, render_graph.graph,
                                                          resources, render_graph.scene_color);
        };
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer terrain",
                .command_buffer_mode = command_buffer_mode,
            },
            render_graph.graph, prepare_resources);
    }

    [[nodiscard]] const cubey::render::Mesh& mesh() const {
        if (!mesh_.has_value()) {
            throw std::runtime_error("terrain clipmap mesh is not initialized");
        }
        return mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& terrain_forward_pass() const {
        if (!terrain_pass_.has_value()) {
            throw std::runtime_error("terrain forward pass is not initialized");
        }
        return terrain_pass_.value();
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<TerrainSourceGpuParameters>&
    source_material() const {
        if (!source_material_.has_value()) {
            throw std::runtime_error("terrain source material is not initialized");
        }
        return source_material_.value();
    }

    RunConfig run_config_;
    TerrainRuntimeConfig runtime_config_{};
    TerrainSourceParameters source_parameters_{};
    TerrainClipmapMeshData clipmap_data_{};
    cubey::render::ClipmapGrid2DConfig clipmap_config_{};
    TerrainSourceSummary scene_summary_{};
    cubey::OrbitController orbit_controller_;
    TerrainSurfaceController surface_controller_{};
    cubey::Camera3D camera_;
    cubey::Transform3D frame_camera_transform_{};
    cubey::AtmosphereEnvironmentRunState atmosphere_state_{};

    std::optional<cubey::render::Mesh> mesh_{};
    std::optional<cubey::render::FrameUniformMaterialInstance<TerrainSourceGpuParameters>>
        source_material_{};
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_atlases_{};
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_{};
    cubey::render::HdrPostFrame hdr_post_frame_{};
    std::optional<cubey::render::ForwardScenePass3D> terrain_pass_{};
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
    bool global_resources_created_ = false;
};

} // namespace

int run_terrain(const cubey::RunConfig& config) {
    TerrainApp app(config);
    return app.run();
}

} // namespace cubey::projects::terrain
