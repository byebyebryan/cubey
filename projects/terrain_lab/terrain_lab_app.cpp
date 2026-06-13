#include "terrain_lab_app.h"

#include "terrain_lab_config.h"
#include "terrain_lab_fields.h"
#include "terrain_lab_mesh.h"

#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_TERRAIN_LAB_SHADER_DIR
#error "CUBEY_TERRAIN_LAB_SHADER_DIR must be defined by the terrain_lab CMake target"
#endif

namespace cubey::projects::terrain_lab {
namespace {

constexpr float kDefaultPitchRadians = -0.88F;

struct TerrainLabPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_debug{0.32F, 0.78F, 0.52F, 0.0F};
    cubey::math::Vec4 field_ranges{0.0F, 1.0F, 1.0F, 1.0F};
    cubey::math::Vec4 contribution_ranges{1.0F, 1.0F, 1.0F, 1.0F};
    cubey::math::Vec4 hydrology_ranges{1.0F, 1.0F, 1.0F, 1.0F};
};

static_assert(sizeof(TerrainLabPushConstants) ==
              sizeof(cubey::math::Mat4) + (sizeof(cubey::math::Vec4) * 4U));
static_assert(sizeof(TerrainLabPushConstants) <= 128U);

struct TerrainLabRenderRanges {
    float max_abs_structure_m = 1.0F;
    float max_abs_process_m = 1.0F;
    float max_abs_detail_m = 1.0F;
    float max_abs_noise_off_m = 1.0F;
    float max_canopy_height_m = 1.0F;
};

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TERRAIN_LAB_SHADER_DIR) / filename;
}

[[nodiscard]] float terrain_lab_extent_m(const TerrainLabConfig& config) {
    return static_cast<float>(std::max(config.grid_width, config.grid_height) - 1U) *
           config.cell_size_m;
}

[[nodiscard]] float terrain_lab_camera_distance(const TerrainLabConfig& config) {
    return std::max(1200.0F, terrain_lab_extent_m(config) * 0.92F);
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_lab_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TerrainLabPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "terrain_lab.forward",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] TerrainLabRenderRanges make_render_ranges(const TerrainLabFieldData& fields) {
    TerrainLabRenderRanges ranges;
    for (std::size_t sample = 0; sample < fields.sample_count(); ++sample) {
        ranges.max_abs_structure_m =
            std::max(ranges.max_abs_structure_m, std::abs(fields.structure_height_m[sample]));
        ranges.max_abs_process_m =
            std::max(ranges.max_abs_process_m, std::abs(fields.process_delta_m[sample]));
        ranges.max_abs_detail_m =
            std::max(ranges.max_abs_detail_m, std::abs(fields.detail_height_m[sample]));
        ranges.max_abs_noise_off_m =
            std::max(ranges.max_abs_noise_off_m,
                     std::abs(fields.structure_height_m[sample] + fields.process_delta_m[sample]));
        ranges.max_canopy_height_m =
            std::max(ranges.max_canopy_height_m, fields.canopy_height_m[sample]);
    }
    return ranges;
}

class TerrainLabApp {
  public:
    explicit TerrainLabApp(RunConfig config)
        : config_(std::move(config)), terrain_config_(terrain_lab_config_from_run_config(config_)),
          fields_(generate_terrain_lab_fields(terrain_config_)),
          mesh_data_(make_terrain_lab_mesh(fields_)), ranges_(make_render_ranges(fields_)),
          orbit_controller_(cubey::OrbitControllerConfig{
              .distance = terrain_lab_camera_distance(terrain_config_),
              .min_distance = std::max(terrain_lab_extent_m(terrain_config_) * 0.02F, 16.0F),
              .max_distance = std::max(terrain_lab_camera_distance(terrain_config_) * 4.0F,
                                       4800.0F),
          }),
          camera_(cubey::Camera3DConfig{.near_z = 0.5F,
                                        .far_z = std::max(terrain_lab_extent_m(terrain_config_) *
                                                              4.0F,
                                                          12000.0F)}) {}

    TerrainLabApp(const TerrainLabApp&) = delete;
    TerrainLabApp& operator=(const TerrainLabApp&) = delete;

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
                                  const FrameTiming& timing) {
            update_input(context, timing);
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_frame(context.device(), frame.command_buffer, frame.color_target,
                         frame.frame_slot, true);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
            (void)timing;
            return cubey::host::FrameStatsSample{
                .width = context.swapchain().extent().width,
                .height = context.swapchain().extent().height,
                .triangles = terrain_lab_triangle_count(mesh_data_),
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "terrain_lab",
                .ready_status = "rendering terrain lab",
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
            record_frame(context.device(), command_buffer, target, frame.frame_slot, false);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void create_global_resources_if_needed(cubey::vulkan::GpuRuntime& gpu) {
        if (mesh_.has_value()) {
            return;
        }
        mesh_.emplace(gpu, mesh_data_.mesh_config());
    }

    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format, std::uint32_t frame_slot_count) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("terrain_lab.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("terrain_lab.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input = terrain_lab_vertex_input_layout();
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
                        .material_pass = terrain_lab_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.58F, 0.70F, 0.78F, 1.0F),
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
        mesh_.reset();
    }

    void update_input(const cubey::host::WindowedAppContext& context,
                      const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::D)) {
            terrain_config_.debug_view = next_terrain_lab_debug_view(terrain_config_.debug_view);
        }
        orbit_controller_.update_from_input(input, timing.delta_seconds);
    }

    [[nodiscard]] TerrainLabPushConstants push_constants(VkExtent2D extent) const {
        const float aspect =
            extent.height == 0U ? 1.0F
                                : static_cast<float>(extent.width) /
                                      static_cast<float>(extent.height);
        const float target_y = fields_.min_height_m +
                               ((fields_.max_height_m - fields_.min_height_m) * 0.18F);
        const cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
            .target = {0.0F, target_y, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw(),
            .pitch = orbit_controller_.pitch() + kDefaultPitchRadians,
        });
        return {
            .view_projection = camera_.view_projection_matrix(camera_transform, aspect),
            .light_direction_debug =
                {
                    0.32F,
                    0.78F,
                    0.52F,
                    static_cast<float>(terrain_config_.debug_view),
                },
            .field_ranges =
                {
                    fields_.min_height_m,
                    fields_.max_height_m,
                    std::max(fields_.max_slope, 0.001F),
                    std::max(fields_.max_abs_curvature, 0.001F),
                },
            .contribution_ranges =
                {
                    ranges_.max_abs_structure_m,
                    ranges_.max_abs_process_m,
                    ranges_.max_abs_detail_m,
                    ranges_.max_abs_noise_off_m,
                },
            .hydrology_ranges =
                {
                    std::max(fields_.max_flow_accumulation, 1.0F),
                    std::max(fields_.max_stream_power, 0.001F),
                    std::max(ranges_.max_canopy_height_m, 1.0F),
                    1.0F,
                },
        };
    }

    void record_frame(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                      cubey::render::ColorTargetView color_target,
                      cubey::render::FrameSlot frame_slot, bool present) {
        const TerrainLabPushConstants constants = push_constants(color_target.extent);
        const auto record = [this, &constants](const cubey::vulkan::CommandRecorder& recorder) {
            recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   forward_pass().pipeline().pipeline());
            recorder.push_constants(forward_pass().pipeline().layout(),
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    constants);
            cubey::render::record_draw_item(recorder.handle(),
                                            cubey::render::DrawItem{.mesh = &mesh()});
        };

        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureState initial_state =
            present ? cubey::render::render_graph_undefined_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureState final_state =
            present ? cubey::render::render_graph_present_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "terrain lab backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("terrain lab depth", forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());

        graph.add_pass("terrain lab scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(backbuffer)
            .write_depth(depth)
            .material_pass(terrain_lab_pass_info())
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
                .label = "vkEndCommandBuffer terrain_lab",
                .command_buffer_mode =
                    present ? cubey::render::RenderGraphCommandBufferMode::BeginAndEnd
                            : cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            frame_graph);
    }

    [[nodiscard]] const cubey::render::Mesh& mesh() const {
        if (!mesh_.has_value()) {
            throw std::runtime_error("terrain lab mesh is not initialized");
        }
        return mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("terrain lab forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    TerrainLabConfig terrain_config_{};
    TerrainLabFieldData fields_{};
    TerrainLabMeshData mesh_data_{};
    TerrainLabRenderRanges ranges_{};
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_;
    std::optional<cubey::render::Mesh> mesh_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
};

} // namespace

int run_terrain_lab(const RunConfig& config) {
    TerrainLabApp app(config);
    return app.run();
}

} // namespace cubey::projects::terrain_lab
