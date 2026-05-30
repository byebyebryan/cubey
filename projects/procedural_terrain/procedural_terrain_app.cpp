#include "procedural_terrain_app_internal.h"

#include <cubey/render/material.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/vk_check.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_PROCEDURAL_TERRAIN_SHADER_DIR
#error "CUBEY_PROCEDURAL_TERRAIN_SHADER_DIR must be defined by the procedural_terrain target"
#endif

namespace cubey::projects::procedural_terrain {
namespace {

constexpr float kDefaultPitchRadians = -0.90F;

[[nodiscard]] float terrain_extent_m(const TerrainConfig& config) {
    return static_cast<float>(std::max(config.grid_width, config.grid_height) - 1U) *
           config.cell_size_m;
}

[[nodiscard]] float terrain_camera_distance(const TerrainConfig& config) {
    return std::max(300.0F, terrain_extent_m(config) * 0.88F);
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TerrainPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "procedural_terrain.forward",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] TerrainDiagnostics make_terrain_diagnostics(const TerrainFieldData& fields,
                                                          const TerrainMeshData& mesh,
                                                          const TerrainMeshData& water_mesh,
                                                          double rebuild_ms,
                                                          std::uint64_t rebuild_count) {
    TerrainDiagnostics diagnostics;
    diagnostics.sample_count = fields.sample_count();
    diagnostics.min_height_m = fields.min_height_m;
    diagnostics.max_height_m = fields.max_height_m;
    diagnostics.max_water_depth_m = fields.max_water_depth_m;
    diagnostics.max_abs_shore_sdf_m = fields.max_abs_shore_sdf_m;
    diagnostics.terrain_vertices = static_cast<std::uint32_t>(mesh.vertices.size());
    diagnostics.terrain_triangles = terrain_triangle_count(mesh);
    diagnostics.water_vertices = static_cast<std::uint32_t>(water_mesh.vertices.size());
    diagnostics.water_triangles = terrain_triangle_count(water_mesh);
    diagnostics.last_rebuild_ms = rebuild_ms;
    diagnostics.rebuild_count = rebuild_count;

    double slope_sum = 0.0;
    for (std::size_t index = 0; index < diagnostics.sample_count; ++index) {
        if (fields.water_depth_m[index] > 0.0F) {
            ++diagnostics.water_samples;
        } else {
            ++diagnostics.land_samples;
        }
        if (std::abs(fields.shore_sdf_m[index]) <= fields.desc.cell_size_m * 1.5F) {
            ++diagnostics.shoreline_samples;
        }
        const float slope = fields.slope[index];
        slope_sum += slope;
        diagnostics.max_slope = std::max(diagnostics.max_slope, slope);

        const TerrainMaterialMask mask = fields.material_masks[index];
        diagnostics.sand_coverage += mask.sand;
        diagnostics.rock_coverage += mask.rock;
        diagnostics.vegetation_coverage += mask.vegetation;
        diagnostics.sediment_coverage += mask.sediment;
    }

    const float inv_samples = diagnostics.sample_count == 0U
                                  ? 0.0F
                                  : 1.0F / static_cast<float>(diagnostics.sample_count);
    diagnostics.average_slope = static_cast<float>(slope_sum) * inv_samples;
    diagnostics.sand_coverage *= inv_samples;
    diagnostics.rock_coverage *= inv_samples;
    diagnostics.vegetation_coverage *= inv_samples;
    diagnostics.sediment_coverage *= inv_samples;
    return diagnostics;
}

} // namespace

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PROCEDURAL_TERRAIN_SHADER_DIR) / filename;
}

ProceduralTerrainApp::ProceduralTerrainApp(RunConfig config)
    : config_(std::move(config)), terrain_config_(terrain_config_from_run_config(config_)),
      edit_terrain_config_(terrain_config_), fields_(generate_terrain_fields(terrain_config_)),
      mesh_data_(make_terrain_mesh(fields_)),
      water_mesh_data_(make_water_surface_mesh(fields_)),
      orbit_controller_(cubey::OrbitControllerConfig{
          .distance = terrain_camera_distance(terrain_config_),
          .min_distance = 48.0F,
          .max_distance = std::max(terrain_camera_distance(terrain_config_) * 4.0F, 960.0F),
      }) {
    if (config_.terrain.water_surface >= 0) {
        water_visible_ = config_.terrain.water_surface != 0;
    }
    refresh_diagnostics(0.0);
}

int ProceduralTerrainApp::run() {
    if (config_.headless) {
        return run_headless();
    }
    return run_windowed();
}

int ProceduralTerrainApp::run_windowed() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context.gpu());
    };
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_forward_pass(context.device(), context.swapchain().extent(),
                            context.swapchain().format());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        update_input(context, timing);
    };
    callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext&,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_terrain_frame(frame.command_buffer, frame.color_target, true);
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
            .app_name = "procedural_terrain",
            .ready_status = "rendering procedural terrain",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int ProceduralTerrainApp::run_headless() {
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    host_config.require_dynamic_rendering = true;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        create_global_resources_if_needed(context.gpu());
        create_forward_pass(context.device(), context.render_target().extent,
                            context.render_target().format);
    };
    callbacks.record_frame =
        [this](cubey::host::HeadlessPngContext&, const cubey::host::HeadlessCaptureFrame&,
               VkCommandBuffer command_buffer, const cubey::host::HeadlessRenderTarget& target) {
            record_terrain_frame(command_buffer, target, false);
        };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

void ProceduralTerrainApp::create_global_resources_if_needed(cubey::vulkan::GpuRuntime& gpu) {
    if (mesh_.has_value()) {
        return;
    }
    mesh_.emplace(gpu, mesh_data_.mesh_config());
    water_mesh_.emplace(gpu, water_mesh_data_.mesh_config());
}

void ProceduralTerrainApp::create_forward_pass(const cubey::vulkan::Device& device,
                                               VkExtent2D extent, VkFormat color_format) {
    const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
        cubey::render::vertex_shader_file(shader_path("procedural_terrain.vert.spv")),
        cubey::render::fragment_shader_file(shader_path("procedural_terrain.frag.spv")),
    };
    const cubey::render::VertexInputLayout vertex_input = terrain_vertex_input_layout();
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
                    .material_pass = terrain_pass_info(),
                },
            .clear =
                {
                    .color = cubey::render::color_clear_value(0.62F, 0.74F, 0.84F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void ProceduralTerrainApp::destroy_swapchain_resources() {
    forward_pass_.reset();
}

void ProceduralTerrainApp::destroy_all_resources() {
    destroy_swapchain_resources();
    water_mesh_.reset();
    mesh_.reset();
}

void ProceduralTerrainApp::draw_ui(cubey::host::WindowedAppContext& context) {
    draw_terrain_ui({
        .active_config = terrain_config_,
        .edit_config = edit_terrain_config_,
        .diagnostics = diagnostics_,
        .latest_frame_stats = latest_frame_stats_,
        .water_visible = water_visible_,
        .rebuild_requested = rebuild_requested_,
        .reset_camera_requested = reset_camera_requested_,
        .latest_fps = latest_fps_,
        .latest_frame_ms = latest_frame_ms_,
    });
    if (reset_camera_requested_) {
        orbit_controller_.reset();
        reset_camera_requested_ = false;
    }
    if (rebuild_requested_) {
        rebuild_terrain_resources(context);
        rebuild_requested_ = false;
    }
}

void ProceduralTerrainApp::update_input(const cubey::host::WindowedAppContext& context,
                                        const FrameTiming& timing) {
    orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
}

void ProceduralTerrainApp::rebuild_terrain_resources(cubey::host::WindowedAppContext& context) {
    const auto start = std::chrono::steady_clock::now();
    validate_terrain_config(edit_terrain_config_);

    const TerrainConfig next_config = edit_terrain_config_;
    TerrainFieldData next_fields = generate_terrain_fields(next_config);
    TerrainMeshData next_mesh_data = make_terrain_mesh(next_fields);
    TerrainMeshData next_water_mesh_data = make_water_surface_mesh(next_fields);

    cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                         "vkDeviceWaitIdle procedural terrain rebuild");
    static_cast<void>(context.gpu().drain());
    water_mesh_.reset();
    mesh_.reset();

    terrain_config_ = next_config;
    edit_terrain_config_ = terrain_config_;
    fields_ = std::move(next_fields);
    mesh_data_ = std::move(next_mesh_data);
    water_mesh_data_ = std::move(next_water_mesh_data);
    mesh_.emplace(context.gpu(), mesh_data_.mesh_config());
    water_mesh_.emplace(context.gpu(), water_mesh_data_.mesh_config());
    static_cast<void>(context.gpu().drain());

    const auto end = std::chrono::steady_clock::now();
    const double rebuild_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    ++rebuild_count_;
    refresh_diagnostics(rebuild_ms);
}

void ProceduralTerrainApp::refresh_diagnostics(double rebuild_ms) {
    diagnostics_ =
        make_terrain_diagnostics(fields_, mesh_data_, water_mesh_data_, rebuild_ms, rebuild_count_);
}

std::optional<cubey::host::FrameStatsSample>
ProceduralTerrainApp::record_frame_stats(VkExtent2D extent, const FrameTiming& timing) {
    latest_frame_ms_ = timing.delta_seconds * 1000.0;
    latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

    const cubey::host::FrameStatsSample sample{
        .delta_seconds = timing.delta_seconds,
        .width = extent.width,
        .height = extent.height,
        .triangles = terrain_triangle_count(mesh_data_) +
                     (water_visible_ ? terrain_triangle_count(water_mesh_data_) : 0U),
    };
    if (std::optional<cubey::host::FrameStatsSnapshot> stats =
            ui_frame_stats_.record_frame(sample);
        stats.has_value()) {
        latest_frame_stats_ = stats.value();
    }
    return sample;
}

TerrainPushConstants ProceduralTerrainApp::push_constants(VkExtent2D extent) const {
    const float aspect = extent.height == 0U
                             ? 1.0F
                             : static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
        .target = {0.0F, 18.0F, 0.0F},
        .distance = orbit_controller_.distance(),
        .yaw = orbit_controller_.yaw(),
        .pitch = orbit_controller_.pitch() + kDefaultPitchRadians,
    });
    return {
        .view_projection = camera_.view_projection_matrix(camera_transform, aspect),
        .light_direction_debug =
            {
                0.35F,
                0.75F,
                0.45F,
                static_cast<float>(terrain_config_.debug_view),
            },
        .field_ranges =
            {
                fields_.min_height_m,
                fields_.max_height_m,
                std::max(fields_.max_water_depth_m, 1.0F),
                std::max(fields_.max_abs_shore_sdf_m, 1.0F),
            },
    };
}

void ProceduralTerrainApp::record_terrain_frame(VkCommandBuffer command_buffer,
                                                cubey::render::ColorTargetView color_target,
                                                bool present) {
    const TerrainPushConstants constants = push_constants(color_target.extent);
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    if (present) {
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    }

    const auto record = [this, &constants](const cubey::vulkan::CommandRecorder& pass_recorder) {
        pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    forward_pass().pipeline().pipeline());
        pass_recorder.push_constants(forward_pass().pipeline().layout(),
                                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                     constants);
        if (terrain_config_.debug_view == TerrainDebugView::Final && water_visible_) {
            cubey::render::record_draw_item(pass_recorder.handle(),
                                            cubey::render::DrawItem{.mesh = &water_mesh()});
        }
        cubey::render::record_draw_item(pass_recorder.handle(),
                                        cubey::render::DrawItem{.mesh = &mesh()});
    };

    if (present) {
        forward_pass().record_to_present_target(recorder, color_target, record);
        recorder.end("vkEndCommandBuffer procedural_terrain");
    } else {
        recorder.transition_image_layout(cubey::vulkan::begin_depth_attachment_transition(
            forward_pass().depth_attachment().handle()));
        forward_pass().record_to_prepared_target(recorder, color_target, record);
    }
}

const cubey::render::Mesh& ProceduralTerrainApp::mesh() const {
    if (!mesh_.has_value()) {
        throw std::runtime_error("procedural terrain mesh is not initialized");
    }
    return mesh_.value();
}

const cubey::render::Mesh& ProceduralTerrainApp::water_mesh() const {
    if (!water_mesh_.has_value()) {
        throw std::runtime_error("procedural terrain water mesh is not initialized");
    }
    return water_mesh_.value();
}

const cubey::render::ForwardScenePass3D& ProceduralTerrainApp::forward_pass() const {
    if (!forward_pass_.has_value()) {
        throw std::runtime_error("procedural terrain forward pass is not initialized");
    }
    return forward_pass_.value();
}

int run_procedural_terrain(const RunConfig& config) {
    ProceduralTerrainApp app(config);
    return app.run();
}

} // namespace cubey::projects::procedural_terrain
