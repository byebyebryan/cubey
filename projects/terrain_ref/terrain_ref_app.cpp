#include "terrain_ref_app.h"

#include "shadertoy_biome_reference.h"
#include "shadertoy_mountain_reference.h"
#include "terrain_engine_reference.h"
#include "terrain_ref_config.h"
#include "terrain_ref_mesh.h"

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
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef CUBEY_TERRAIN_REF_SHADER_DIR
#error "CUBEY_TERRAIN_REF_SHADER_DIR must be defined by the terrain_ref CMake target"
#endif

namespace cubey::projects::terrain_ref {
namespace {

struct TerrainRefCameraFrame {
    float pitch_radians = -0.70F;
    float yaw_radians = 0.58F;
    float distance_extent_scale = 1.08F;
    float target_height_fraction = 0.42F;
};

struct TerrainRefPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_extent{0.38F, 0.82F, 0.42F, 1.0F};
    cubey::math::Vec4 terrain_params{0.0F, 0.0F, 1.0F, 1.0F};
    cubey::math::Vec4 water_params{kTerrainEngineReferenceWaterHeightM, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 camera_position_fog{0.0F, 0.0F, 0.0F, 1.5e-6F};
};

static_assert(sizeof(TerrainRefPushConstants) ==
              sizeof(cubey::math::Mat4) + (4U * sizeof(cubey::math::Vec4)));
static_assert(sizeof(TerrainRefPushConstants) <= 128U);

struct TerrainRefSceneMetrics {
    float min_height_m = 0.0F;
    float max_height_m = 1.0F;
    float scene_extent_m = 1.0F;
};

[[nodiscard]] float terrain_ref_material_mode_id(TerrainRefMaterialMode mode) {
    return mode == TerrainRefMaterialMode::Height ? 1.0F : 0.0F;
}

[[nodiscard]] float terrain_ref_source_id(TerrainRefRecipe recipe) {
    switch (recipe) {
    case TerrainRefRecipe::TerrainEngine:
        return 0.0F;
    case TerrainRefRecipe::ShadertoyMountain:
        return 1.0F;
    case TerrainRefRecipe::ShadertoyAlpine:
        return 2.0F;
    case TerrainRefRecipe::ShadertoyDunes:
        return 3.0F;
    case TerrainRefRecipe::ShadertoyLakeBasin:
        return 4.0F;
    }
    return 0.0F;
}

[[nodiscard]] float terrain_ref_water_height_m(TerrainRefRecipe recipe) {
    switch (recipe) {
    case TerrainRefRecipe::TerrainEngine:
        return kTerrainEngineReferenceWaterHeightM;
    case TerrainRefRecipe::ShadertoyMountain:
        return kShadertoyMountainReferenceWaterHeightM;
    case TerrainRefRecipe::ShadertoyAlpine:
        return kShadertoyAlpineReferenceWaterHeightM;
    case TerrainRefRecipe::ShadertoyDunes:
        return kShadertoyDunesReferenceWaterHeightM;
    case TerrainRefRecipe::ShadertoyLakeBasin:
        return kShadertoyLakeBasinReferenceWaterHeightM;
    }
    return kTerrainEngineReferenceWaterHeightM;
}

[[nodiscard]] float terrain_ref_height_m(const TerrainRefConfig& config, float world_x,
                                         float world_z) {
    switch (config.recipe) {
    case TerrainRefRecipe::TerrainEngine:
        return terrain_engine_reference_height(world_x, world_z, config.seed);
    case TerrainRefRecipe::ShadertoyMountain:
        return shadertoy_mountain_reference_height(world_x, world_z, config.seed);
    case TerrainRefRecipe::ShadertoyAlpine:
        return shadertoy_alpine_reference_height(world_x, world_z, config.seed);
    case TerrainRefRecipe::ShadertoyDunes:
        return shadertoy_dunes_reference_height(world_x, world_z, config.seed);
    case TerrainRefRecipe::ShadertoyLakeBasin:
        return shadertoy_lake_basin_reference_height(world_x, world_z, config.seed);
    }
    return terrain_engine_reference_height(world_x, world_z, config.seed);
}

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TERRAIN_REF_SHADER_DIR) / filename;
}

[[nodiscard]] TerrainRefCameraFrame terrain_ref_camera_frame(TerrainRefCameraPreset preset) {
    switch (preset) {
    case TerrainRefCameraPreset::Oblique:
        return {};
    case TerrainRefCameraPreset::Profile:
        return {
            .pitch_radians = -0.22F,
            .yaw_radians = 0.92F,
            .distance_extent_scale = 1.00F,
            .target_height_fraction = 0.34F,
        };
    case TerrainRefCameraPreset::Top:
        return {
            .pitch_radians = -1.43F,
            .yaw_radians = 0.20F,
            .distance_extent_scale = 1.15F,
            .target_height_fraction = 0.35F,
        };
    case TerrainRefCameraPreset::Surface:
        return {
            .pitch_radians = -0.26F,
            .yaw_radians = 0.62F,
            .distance_extent_scale = 0.40F,
            .target_height_fraction = 0.32F,
        };
    case TerrainRefCameraPreset::SurfaceLow:
        return {
            .pitch_radians = -0.42F,
            .yaw_radians = 0.62F,
            .distance_extent_scale = 0.34F,
            .target_height_fraction = 0.34F,
        };
    }
    return {};
}

[[nodiscard]] TerrainRefSceneMetrics terrain_ref_scene_metrics(const TerrainRefConfig& config) {
    const cubey::render::ClipmapGrid2DConfig clipmap_config = terrain_ref_clipmap_config(config);
    constexpr std::uint32_t kSamplesPerAxis = 65U;
    float min_height = std::numeric_limits<float>::max();
    float max_height = std::numeric_limits<float>::lowest();
    for (std::uint32_t z = 0; z < kSamplesPerAxis; ++z) {
        const float z_t = static_cast<float>(z) / static_cast<float>(kSamplesPerAxis - 1U);
        const float world_z =
            -clipmap_config.outer_half_extent + (2.0F * clipmap_config.outer_half_extent * z_t);
        for (std::uint32_t x = 0; x < kSamplesPerAxis; ++x) {
            const float x_t = static_cast<float>(x) / static_cast<float>(kSamplesPerAxis - 1U);
            const float world_x =
                -clipmap_config.outer_half_extent + (2.0F * clipmap_config.outer_half_extent * x_t);
            const float height = terrain_ref_height_m(config, world_x, world_z);
            min_height = std::min(min_height, height);
            max_height = std::max(max_height, height);
        }
    }
    const float height_extent =
        std::max((max_height - min_height) * config.vertical_scale, 1.0F) * 2.25F;
    return {
        .min_height_m = min_height,
        .max_height_m = max_height,
        .scene_extent_m = std::max(clipmap_config.outer_half_extent * 2.0F, height_extent),
    };
}

[[nodiscard]] float terrain_ref_camera_distance(const TerrainRefConfig& config,
                                                const TerrainRefSceneMetrics& metrics) {
    const TerrainRefCameraFrame frame = terrain_ref_camera_frame(config.camera_preset);
    return std::max(1200.0F, metrics.scene_extent_m * frame.distance_extent_scale);
}

[[nodiscard]] bool terrain_ref_is_surface_camera(TerrainRefCameraPreset preset) {
    return preset == TerrainRefCameraPreset::Surface ||
           preset == TerrainRefCameraPreset::SurfaceLow;
}

[[nodiscard]] float terrain_ref_vertical_extent_m(const TerrainRefConfig& config,
                                                  const TerrainRefSceneMetrics& metrics) {
    return std::max((metrics.max_height_m - metrics.min_height_m) * config.vertical_scale, 1.0F);
}

[[nodiscard]] float terrain_ref_surface_camera_clearance_m(const TerrainRefConfig& config,
                                                           const TerrainRefSceneMetrics& metrics) {
    return std::clamp(terrain_ref_vertical_extent_m(config, metrics) * 0.04F, 48.0F, 240.0F);
}

[[nodiscard]] float terrain_ref_camera_target_y_m(const TerrainRefConfig& config,
                                                  const TerrainRefSceneMetrics& metrics) {
    const TerrainRefCameraFrame frame = terrain_ref_camera_frame(config.camera_preset);
    const float min_height = metrics.min_height_m * config.vertical_scale;
    const float max_height = metrics.max_height_m * config.vertical_scale;
    float target_y = min_height + ((max_height - min_height) * frame.target_height_fraction);
    return target_y;
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_ref_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TerrainRefPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "terrain_ref.forward",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

class TerrainRefApp {
  public:
    explicit TerrainRefApp(RunConfig config)
        : config_(std::move(config)), terrain_config_(terrain_ref_config_from_run_config(config_)),
          mesh_data_(make_terrain_ref_mesh(terrain_config_)),
          scene_metrics_(terrain_ref_scene_metrics(terrain_config_)),
          orbit_controller_(cubey::OrbitControllerConfig{
              .distance = terrain_ref_camera_distance(terrain_config_, scene_metrics_),
              .min_distance = std::max(scene_metrics_.scene_extent_m * 0.02F, 24.0F),
              .max_distance = std::max(
                  terrain_ref_camera_distance(terrain_config_, scene_metrics_) * 4.0F, 4800.0F),
          }),
          camera_(cubey::Camera3DConfig{
              .near_z = 1.0F,
              .far_z = std::max(scene_metrics_.scene_extent_m * 5.0F, 16000.0F),
          }) {}

    TerrainRefApp(const TerrainRefApp&) = delete;
    TerrainRefApp& operator=(const TerrainRefApp&) = delete;

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
                                  const FrameTiming& timing) {
            orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
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
                .triangles = terrain_ref_triangle_count(mesh_data_),
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "terrain_ref",
                .ready_status = "rendering terrain reference",
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
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("terrain_ref.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("terrain_ref.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_input_layout();
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
                        .material_pass = terrain_ref_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.54F, 0.64F, 0.68F, 1.0F),
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

    [[nodiscard]] TerrainRefPushConstants push_constants(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const TerrainRefCameraFrame frame = terrain_ref_camera_frame(terrain_config_.camera_preset);
        const float target_y = terrain_ref_camera_target_y_m(terrain_config_, scene_metrics_);
        cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
            .target = {0.0F, target_y, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw() + frame.yaw_radians,
            .pitch = orbit_controller_.pitch() + frame.pitch_radians,
        });
        if (terrain_ref_is_surface_camera(terrain_config_.camera_preset)) {
            const float camera_surface_y =
                terrain_ref_height_m(terrain_config_, camera_transform.translation.x,
                                     camera_transform.translation.z) *
                terrain_config_.vertical_scale;
            const float min_camera_y =
                camera_surface_y +
                terrain_ref_surface_camera_clearance_m(terrain_config_, scene_metrics_);
            camera_transform.translation.y = std::max(camera_transform.translation.y, min_camera_y);
        }
        const TerrainEngineReferenceSeedComponents seed_components =
            terrain_engine_reference_seed_components(terrain_config_.seed);
        return {
            .view_projection = camera_.view_projection_matrix(camera_transform, aspect),
            .light_direction_extent =
                {
                    0.38F,
                    0.82F,
                    0.42F,
                    scene_metrics_.scene_extent_m,
                },
            .terrain_params =
                {
                    seed_components.x,
                    seed_components.y,
                    terrain_config_.vertical_scale,
                    terrain_ref_source_id(terrain_config_.recipe),
                },
            .water_params =
                {
                    terrain_config_.water_surface ? terrain_ref_water_height_m(terrain_config_.recipe)
                                                  : -100000.0F,
                    scene_metrics_.min_height_m,
                    scene_metrics_.max_height_m,
                    terrain_ref_material_mode_id(terrain_config_.material_mode),
                },
            .camera_position_fog =
                {
                    camera_transform.translation.x,
                    camera_transform.translation.y,
                    camera_transform.translation.z,
                    1.5e-6F,
                },
        };
    }

    void record_frame(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                      cubey::render::ColorTargetView color_target,
                      cubey::render::FrameSlot frame_slot, bool present) {
        (void)frame_slot;
        const TerrainRefPushConstants constants = push_constants(color_target.extent);
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
            "terrain ref backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("terrain ref depth", forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());

        graph.add_pass("terrain ref scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(backbuffer)
            .write_depth(depth)
            .material_pass(terrain_ref_pass_info())
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
                .label = "vkEndCommandBuffer terrain_ref",
                .command_buffer_mode =
                    present ? cubey::render::RenderGraphCommandBufferMode::BeginAndEnd
                            : cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            frame_graph);
    }

    [[nodiscard]] const cubey::render::Mesh& mesh() const {
        if (!mesh_.has_value()) {
            throw std::runtime_error("terrain_ref mesh is not initialized");
        }
        return mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("terrain_ref forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    TerrainRefConfig terrain_config_{};
    TerrainRefMeshData mesh_data_{};
    TerrainRefSceneMetrics scene_metrics_{};
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_;
    std::optional<cubey::render::Mesh> mesh_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
};

} // namespace

int run_terrain_ref(const cubey::RunConfig& config) {
    TerrainRefApp app(config);
    return app.run();
}

} // namespace cubey::projects::terrain_ref
