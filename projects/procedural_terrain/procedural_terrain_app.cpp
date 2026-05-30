#include "procedural_terrain_app_internal.h"

#include <cubey/render/material.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image_transitions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_PROCEDURAL_TERRAIN_SHADER_DIR
#error "CUBEY_PROCEDURAL_TERRAIN_SHADER_DIR must be defined by the procedural_terrain target"
#endif

namespace cubey::projects::procedural_terrain {
namespace {

constexpr float kDefaultPitchRadians = -0.68F;

[[nodiscard]] float terrain_extent_m(const TerrainConfig& config) {
    return static_cast<float>(std::max(config.grid_width, config.grid_height) - 1U) *
           config.cell_size_m;
}

[[nodiscard]] float terrain_camera_distance(const TerrainConfig& config) {
    return std::max(320.0F, terrain_extent_m(config) * 1.18F);
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

} // namespace

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PROCEDURAL_TERRAIN_SHADER_DIR) / filename;
}

ProceduralTerrainApp::ProceduralTerrainApp(RunConfig config)
    : config_(std::move(config)), terrain_config_(terrain_config_from_run_config(config_)),
      fields_(generate_terrain_fields(terrain_config_)), mesh_data_(make_terrain_mesh(fields_)),
      orbit_controller_(cubey::OrbitControllerConfig{
          .distance = terrain_camera_distance(terrain_config_),
          .min_distance = 48.0F,
          .max_distance = std::max(terrain_camera_distance(terrain_config_) * 4.0F, 960.0F),
      }) {}

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
    callbacks.record_frame = [this](cubey::host::WindowedAppContext&,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_terrain_frame(frame.command_buffer, frame.color_target, true);
    };
    callbacks.frame_stats_sample =
        [this](cubey::host::WindowedAppContext& context,
               const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        return cubey::host::FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = terrain_triangle_count(mesh_data_),
        };
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
                    .color = cubey::render::color_clear_value(0.48F, 0.60F, 0.70F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void ProceduralTerrainApp::destroy_swapchain_resources() {
    forward_pass_.reset();
}

void ProceduralTerrainApp::destroy_all_resources() {
    destroy_swapchain_resources();
    mesh_.reset();
}

void ProceduralTerrainApp::update_input(const cubey::host::WindowedAppContext& context,
                                        const FrameTiming& timing) {
    orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
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
