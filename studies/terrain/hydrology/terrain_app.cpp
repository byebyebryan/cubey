#include "terrain_app.h"

#include "terrain_mesh.h"
#include "terrain_patch.h"

#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>
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
#include <string>
#include <utility>

#ifndef CUBEY_TERRAIN_HYDROLOGY_LAB_SHADER_DIR
#error "CUBEY_TERRAIN_HYDROLOGY_LAB_SHADER_DIR must be defined by the terrain hydrology lab CMake target"
#endif

namespace cubey::projects::terrain_hydrology_lab {
namespace {

struct TerrainCameraFrame {
    float pitch_radians = -0.68F;
    float yaw_radians = 0.62F;
    float distance_scale = 0.92F;
    float target_height_fraction = 0.34F;
    bool surface = false;
};

struct TerrainSceneMetrics {
    float min_height_m = 0.0F;
    float max_height_m = 1.0F;
    float horizontal_extent_m = 1.0F;
    float scene_extent_m = 1.0F;
};

struct TerrainPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_extent{0.38F, 0.82F, 0.42F, 1.0F};
    cubey::math::Vec4 camera_position_fog{0.0F, 0.0F, 0.0F, 1.0F};
};

static_assert(sizeof(TerrainPushConstants) ==
              sizeof(cubey::math::Mat4) + (2U * sizeof(cubey::math::Vec4)));
static_assert(sizeof(TerrainPushConstants) <= 128U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TERRAIN_HYDROLOGY_LAB_SHADER_DIR) / filename;
}

[[nodiscard]] TerrainPatchRequest request_from_run_config(const cubey::RunConfig& config) {
    TerrainPatchRequest request = default_terrain_patch_request();
    if (config.grid.width != 0U) {
        request.domain.interior_grid.width = config.grid.width;
    }
    if (config.grid.height != 0U) {
        request.domain.interior_grid.height = config.grid.height;
    }
    if (cubey::run_config_float_is_set(config.terrain.cell_size)) {
        request.domain.interior_grid.cell_size = config.terrain.cell_size;
    }
    if (config.terrain.seed_set) {
        request.domain.seed = config.terrain.seed;
    }
    if (!config.terrain.recipe.empty()) {
        request.recipe_id = config.terrain.recipe;
        request.generator_revision = terrain_generator_revision_for_recipe(request.recipe_id);
    }
    return request;
}

[[nodiscard]] float vertical_scale_from_run_config(const cubey::RunConfig& config) {
    return cubey::run_config_float_is_set(config.terrain.vertical_scale)
               ? config.terrain.vertical_scale
               : 1.0F;
}

[[nodiscard]] std::string debug_view_from_run_config(const cubey::RunConfig& config) {
    return config.debug_view.empty() ? "surface" : config.debug_view;
}

[[nodiscard]] std::string camera_preset_from_run_config(const cubey::RunConfig& config) {
    return config.terrain.camera_preset.empty() ? "oblique" : config.terrain.camera_preset;
}

[[nodiscard]] TerrainCameraFrame camera_frame(std::string_view preset) {
    if (preset == "oblique" || preset == "coastal-oblique") {
        return {};
    }
    if (preset == "profile") {
        return {.pitch_radians = -0.24F,
                .yaw_radians = 0.92F,
                .distance_scale = 0.88F,
                .target_height_fraction = 0.32F};
    }
    if (preset == "top") {
        return {.pitch_radians = -1.43F,
                .yaw_radians = 0.24F,
                .distance_scale = 1.08F,
                .target_height_fraction = 0.34F};
    }
    if (preset == "surface" || preset == "surface-low") {
        return {.pitch_radians = preset == "surface-low" ? -0.18F : -0.28F,
                .yaw_radians = 0.62F,
                .distance_scale = preset == "surface-low" ? 0.26F : 0.38F,
                .target_height_fraction = 0.28F,
                .surface = true};
    }
    throw std::runtime_error("unknown terrain camera preset: " + std::string(preset));
}

[[nodiscard]] TerrainSceneMetrics scene_metrics(const TerrainPatchProduct& product,
                                                float vertical_scale) {
    const cubey::procedural::ScalarFieldStats height =
        product.fields.summarize_field(kTerrainFieldHeightM);
    const cubey::procedural::Grid2DDesc& desc = product.fields.desc();
    const float extent_x = static_cast<float>(desc.width - 1U) * desc.cell_size;
    const float extent_z = static_cast<float>(desc.height - 1U) * desc.cell_size;
    const float horizontal = std::max(extent_x, extent_z);
    const float vertical = std::max(height.span * vertical_scale, 1.0F);
    return {.min_height_m = height.min * vertical_scale,
            .max_height_m = height.max * vertical_scale,
            .horizontal_extent_m = horizontal,
            .scene_extent_m = std::max(horizontal, vertical * 2.2F)};
}

[[nodiscard]] float terrain_height_nearest(const TerrainPatchProduct& product, float world_x,
                                           float world_z, float vertical_scale) {
    const cubey::procedural::ScalarField2D& height = product.fields.field(kTerrainFieldHeightM);
    const cubey::procedural::Grid2DDesc& desc = height.desc();
    const float min_x = cubey::procedural::grid_sample_x(desc, 0U);
    const float min_z = cubey::procedural::grid_sample_y(desc, 0U);
    const auto x = static_cast<std::uint32_t>(std::clamp(
        std::round((world_x - min_x) / desc.cell_size), 0.0F, static_cast<float>(desc.width - 1U)));
    const auto z =
        static_cast<std::uint32_t>(std::clamp(std::round((world_z - min_z) / desc.cell_size), 0.0F,
                                              static_cast<float>(desc.height - 1U)));
    return height.at(x, z) * vertical_scale;
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TerrainPushConstants),
    };
    return {.label = "terrain.forward",
            .push_constants = {push_constant_range},
            .cull_mode = VK_CULL_MODE_NONE,
            .depth_test = true,
            .depth_write = true};
}

class TerrainApp {
  public:
    explicit TerrainApp(cubey::RunConfig config)
        : config_(std::move(config)), request_(request_from_run_config(config_)),
          product_(generate_terrain_patch(request_)),
          debug_view_(debug_view_from_run_config(config_)),
          camera_preset_(camera_preset_from_run_config(config_)),
          vertical_scale_(vertical_scale_from_run_config(config_)),
          mesh_data_(make_terrain_mesh(product_, debug_view_, vertical_scale_)),
          metrics_(scene_metrics(product_, vertical_scale_)),
          orbit_controller_(cubey::OrbitControllerConfig{
              .distance = metrics_.scene_extent_m * camera_frame(camera_preset_).distance_scale,
              .min_distance = std::max(metrics_.horizontal_extent_m * 0.02F, 32.0F),
              .max_distance = std::max(metrics_.scene_extent_m * 4.0F, 8000.0F),
          }),
          camera_(cubey::Camera3DConfig{
              .near_z = 1.0F,
              .far_z = std::max(metrics_.scene_extent_m * 6.0F, 20000.0F),
          }) {}

    TerrainApp(const TerrainApp&) = delete;
    TerrainApp& operator=(const TerrainApp&) = delete;

    int run() {
        return config_.headless ? run_headless() : run_windowed();
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
                                  const cubey::FrameTiming& timing) {
            orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_frame(context.device(), frame.command_buffer, frame.color_target,
                         frame.frame_slot, true);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const cubey::FrameTiming&) -> std::optional<cubey::host::FrameStatsSample> {
            return cubey::host::FrameStatsSample{
                .width = context.swapchain().extent().width,
                .height = context.swapchain().extent().height,
                .triangles = terrain_mesh_triangle_count(mesh_data_),
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };
        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "terrain_hydrology",
                .ready_status = "rendering archived terrain hydrology product",
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
        if (!mesh_.has_value()) {
            mesh_.emplace(gpu, mesh_data_.mesh_config());
        }
    }

    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format, std::uint32_t frame_slot_count) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("terrain.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("terrain.frag.spv")),
        };
        const cubey::render::VertexInputLayout input =
            cubey::render::vertex_position_color_normal_input_layout();
        forward_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{.extent = extent,
                                                      .color_format = color_format},
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = shaders,
                        .vertex_bindings = input.bindings(),
                        .vertex_attributes = input.attribute_descriptions(),
                        .material_pass = terrain_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.52F, 0.64F, 0.72F, 1.0F),
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

    [[nodiscard]] TerrainPushConstants push_constants(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const TerrainCameraFrame frame = camera_frame(camera_preset_);
        const float target_y =
            metrics_.min_height_m +
            ((metrics_.max_height_m - metrics_.min_height_m) * frame.target_height_fraction);
        const cubey::procedural::Grid2DDesc& desc = product_.fields.desc();
        cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
            .target = {desc.origin_x, target_y, desc.origin_y},
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw() + frame.yaw_radians,
            .pitch = orbit_controller_.pitch() + frame.pitch_radians,
        });
        if (frame.surface) {
            const float clearance =
                std::clamp((metrics_.max_height_m - metrics_.min_height_m) * 0.04F, 48.0F, 220.0F);
            camera_transform.translation.y =
                std::max(camera_transform.translation.y,
                         terrain_height_nearest(product_, camera_transform.translation.x,
                                                camera_transform.translation.z, vertical_scale_) +
                             clearance);
        }
        return {
            .view_projection = camera_.view_projection_matrix(camera_transform, aspect),
            .light_direction_extent = {0.38F, 0.82F, 0.42F, metrics_.scene_extent_m},
            .camera_position_fog = {camera_transform.translation.x, camera_transform.translation.y,
                                    camera_transform.translation.z, 1.2e-6F},
        };
    }

    void record_frame(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                      cubey::render::ColorTargetView color_target,
                      cubey::render::FrameSlot frame_slot, bool present) {
        const TerrainPushConstants constants = push_constants(color_target.extent);
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
        const cubey::render::RenderGraphTextureState initial =
            present ? cubey::render::render_graph_undefined_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureState final =
            present ? cubey::render::render_graph_present_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureHandle backbuffer =
            graph.import_color_target("terrain backbuffer", color_target, initial, final);
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("terrain depth", forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());
        graph.add_pass("terrain scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(backbuffer)
            .write_depth(depth)
            .material_pass(terrain_pass_info())
            .execute([this, backbuffer, depth,
                      record](const cubey::render::RenderGraphExecutionContext& context) {
                cubey::render::record_render_target_pass(
                    context.recorder(),
                    cubey::render::render_target_view(
                        cubey::render::resolved_color_target_view(context, backbuffer),
                        cubey::render::resolved_depth_target_view(context, depth)),
                    forward_pass().clear_values(), record);
            });
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer terrain",
                .command_buffer_mode =
                    present ? cubey::render::RenderGraphCommandBufferMode::BeginAndEnd
                            : cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            graph.compile());
    }

    [[nodiscard]] const cubey::render::Mesh& mesh() const {
        if (!mesh_.has_value()) {
            throw std::runtime_error("terrain mesh is not initialized");
        }
        return mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("terrain forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    cubey::RunConfig config_;
    TerrainPatchRequest request_{};
    TerrainPatchProduct product_{};
    std::string debug_view_{};
    std::string camera_preset_{};
    float vertical_scale_ = 1.0F;
    TerrainMeshData mesh_data_{};
    TerrainSceneMetrics metrics_{};
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_;
    std::optional<cubey::render::Mesh> mesh_{};
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_{};
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
};

} // namespace

int run_terrain(const cubey::RunConfig& config) {
    TerrainApp app(config);
    return app.run();
}

} // namespace cubey::projects::terrain_hydrology_lab
