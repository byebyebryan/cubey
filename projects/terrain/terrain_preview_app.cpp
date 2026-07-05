#include "terrain_preview_app.h"

#include "terrain_engine_reference.h"
#include "terrain_generator.h"
#include "terrain_phase_profile.h"
#include "terrain_preview_config.h"
#include "terrain_preview_mesh.h"

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

#ifndef CUBEY_TERRAIN_PREVIEW_SHADER_DIR
#error "CUBEY_TERRAIN_PREVIEW_SHADER_DIR must be defined by the terrain_preview CMake target"
#endif

namespace cubey::projects::terrain {
namespace {

struct TerrainPreviewCameraFrame {
    float pitch_radians = -0.70F;
    float yaw_radians = 0.58F;
    float distance_extent_scale = 1.08F;
    float target_height_fraction = 0.42F;
};

struct TerrainPreviewPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_extent{0.38F, 0.82F, 0.42F, 1.0F};
    cubey::math::Vec4 terrain_runtime_params{0.0F, 0.0F, 1.0F, 0.0F};
    cubey::math::Vec4 water_params{kTerrainEngineReferenceWaterHeightM, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(TerrainPreviewPushConstants) ==
              sizeof(cubey::math::Mat4) + (3U * sizeof(cubey::math::Vec4)));
static_assert(sizeof(TerrainPreviewPushConstants) <= 128U);

struct TerrainPreviewSceneMetrics {
    float min_height_m = 0.0F;
    float max_height_m = 1.0F;
    float scene_extent_m = 1.0F;
};

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_TERRAIN_PREVIEW_SHADER_DIR) / filename;
}

[[nodiscard]] float terrain_preview_extent_m(const TerrainRegionConfig& config) {
    return static_cast<float>(std::max(config.grid_width, config.grid_height) - 1U) *
           config.cell_size_m;
}

[[nodiscard]] TerrainPreviewCameraFrame
terrain_preview_camera_frame(TerrainPreviewCameraPreset preset) {
    switch (preset) {
    case TerrainPreviewCameraPreset::Oblique:
        return {};
    case TerrainPreviewCameraPreset::Profile:
        return {
            .pitch_radians = -0.22F,
            .yaw_radians = 0.92F,
            .distance_extent_scale = 1.00F,
            .target_height_fraction = 0.34F,
        };
    case TerrainPreviewCameraPreset::Top:
        return {
            .pitch_radians = -1.43F,
            .yaw_radians = 0.20F,
            .distance_extent_scale = 1.15F,
            .target_height_fraction = 0.35F,
        };
    case TerrainPreviewCameraPreset::Surface:
        return {
            .pitch_radians = -0.26F,
            .yaw_radians = 0.62F,
            .distance_extent_scale = 0.40F,
            .target_height_fraction = 0.32F,
        };
    case TerrainPreviewCameraPreset::SurfaceLow:
        return {
            .pitch_radians = -0.08F,
            .yaw_radians = 0.62F,
            .distance_extent_scale = 0.22F,
            .target_height_fraction = 0.22F,
        };
    }
    return {};
}

[[nodiscard]] TerrainRegionProduct
generate_terrain_region_profiled(const TerrainRegionConfig& config,
                                 TerrainPhaseProfile& phase_profile) {
    TerrainPhaseScope phase(phase_profile, "generate_region");
    return generate_terrain_region(config);
}

[[nodiscard]] std::optional<TerrainRegionProduct>
generate_terrain_preview_product_profiled(const TerrainPreviewConfig& config,
                                          TerrainPhaseProfile& phase_profile) {
    if (config.runtime_mode == TerrainPreviewRuntimeMode::TerrainEngineReference) {
        return std::nullopt;
    }
    return generate_terrain_region_profiled(config.region, phase_profile);
}

[[nodiscard]] TerrainPreviewMeshData
make_terrain_preview_mesh_profiled(const std::optional<TerrainRegionProduct>& product,
                                   const TerrainPreviewConfig& config,
                                   TerrainPhaseProfile& phase_profile) {
    if (config.runtime_mode == TerrainPreviewRuntimeMode::TerrainEngineReference) {
        TerrainPhaseScope phase(phase_profile, "build_runtime_clipmap_mesh");
        return make_terrain_engine_runtime_preview_mesh(config);
    }
    if (!product.has_value()) {
        throw std::runtime_error("terrain CPU preview requires a generated product");
    }
    TerrainPhaseScope phase(phase_profile, "build_preview_mesh");
    return make_terrain_preview_mesh(product.value(), config);
}

[[nodiscard]] TerrainPhaseProfileMetadata
terrain_preview_phase_metadata(const TerrainPreviewConfig& preview_config,
                               const std::optional<TerrainRegionProduct>& product,
                               const TerrainPreviewMeshData& mesh_data) {
    TerrainPhaseProfileMetadata metadata =
        terrain_phase_profile_metadata("terrain_preview", preview_config.region);
    metadata.camera_preset =
        std::string(terrain_preview_camera_preset_name(preview_config.camera_preset));
    metadata.preview_runtime =
        std::string(terrain_preview_runtime_mode_name(preview_config.runtime_mode));
    metadata.preview_surface = std::string(terrain_preview_surface_name(preview_config.surface));
    metadata.preview_color =
        std::string(terrain_preview_color_mode_name(preview_config.color_mode));
    metadata.field_count =
        product.has_value() ? static_cast<std::uint32_t>(product->fields.field_count()) : 0U;
    metadata.output_count = 1U;
    metadata.vertex_count = static_cast<std::uint64_t>(mesh_data.vertices.size());
    metadata.index_count = static_cast<std::uint64_t>(mesh_data.indices.size());
    metadata.triangle_count = terrain_preview_triangle_count(mesh_data);
    return metadata;
}

[[nodiscard]] float terrain_preview_height_span_m(float min_height_m, float max_height_m,
                                                  float vertical_scale) {
    return std::max((max_height_m - min_height_m) * vertical_scale, 1.0F);
}

[[nodiscard]] TerrainPreviewSceneMetrics
terrain_engine_runtime_preview_scene_metrics(const TerrainPreviewConfig& config) {
    const cubey::render::ClipmapGrid2DConfig clipmap_config =
        terrain_engine_runtime_preview_clipmap_config(config);
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
            const float height =
                terrain_engine_reference_height(world_x, world_z, config.region.seed);
            min_height = std::min(min_height, height);
            max_height = std::max(max_height, height);
        }
    }
    const float height_extent =
        terrain_preview_height_span_m(min_height, max_height, config.vertical_scale) * 2.25F;
    return {
        .min_height_m = min_height,
        .max_height_m = max_height,
        .scene_extent_m = std::max(clipmap_config.outer_half_extent * 2.0F, height_extent),
    };
}

[[nodiscard]] TerrainPreviewSceneMetrics
terrain_preview_scene_metrics(const TerrainPreviewConfig& config,
                              const std::optional<TerrainRegionProduct>& product) {
    if (config.runtime_mode == TerrainPreviewRuntimeMode::TerrainEngineReference) {
        return terrain_engine_runtime_preview_scene_metrics(config);
    }
    if (!product.has_value()) {
        throw std::runtime_error("terrain CPU preview requires generated product metrics");
    }
    const float height_extent =
        terrain_preview_height_span_m(product->summary.height.min, product->summary.height.max,
                                      config.vertical_scale) *
        2.25F;
    return {
        .min_height_m = product->summary.height.min,
        .max_height_m = product->summary.height.max,
        .scene_extent_m = std::max(terrain_preview_extent_m(config.region), height_extent),
    };
}

[[nodiscard]] float terrain_preview_camera_distance(const TerrainPreviewConfig& config,
                                                    const TerrainPreviewSceneMetrics& metrics) {
    const TerrainPreviewCameraFrame frame = terrain_preview_camera_frame(config.camera_preset);
    return std::max(1200.0F, metrics.scene_extent_m * frame.distance_extent_scale);
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_preview_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TerrainPreviewPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "terrain_preview.forward",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

class TerrainPreviewApp {
  public:
    explicit TerrainPreviewApp(RunConfig config)
        : config_(std::move(config)),
          preview_config_(terrain_preview_config_from_run_config(config_)),
          total_start_(TerrainPhaseProfile::now()), phase_profile_(config_.profile_output_prefix),
          product_(generate_terrain_preview_product_profiled(preview_config_, phase_profile_)),
          mesh_data_(make_terrain_preview_mesh_profiled(product_, preview_config_, phase_profile_)),
          scene_metrics_(terrain_preview_scene_metrics(preview_config_, product_)),
          orbit_controller_(cubey::OrbitControllerConfig{
              .distance = terrain_preview_camera_distance(preview_config_, scene_metrics_),
              .min_distance = std::max(scene_metrics_.scene_extent_m * 0.02F, 24.0F),
              .max_distance = std::max(
                  terrain_preview_camera_distance(preview_config_, scene_metrics_) * 4.0F, 4800.0F),
          }),
          camera_(cubey::Camera3DConfig{
              .near_z = 1.0F,
              .far_z = std::max(scene_metrics_.scene_extent_m * 5.0F, 16000.0F),
          }) {
        phase_profile_.set_metadata(
            terrain_preview_phase_metadata(preview_config_, product_, mesh_data_));
    }

    TerrainPreviewApp(const TerrainPreviewApp&) = delete;
    TerrainPreviewApp& operator=(const TerrainPreviewApp&) = delete;

    int run() {
        const int result = config_.headless ? run_headless() : run_windowed();
        phase_profile_.record_elapsed("total", total_start_);
        phase_profile_.write();
        return result;
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
                .triangles = terrain_preview_triangle_count(mesh_data_),
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        TerrainPhaseScope phase(phase_profile_, "run_preview_host");
        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "terrain_preview",
                .ready_status = "rendering terrain preview",
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
        TerrainPhaseScope phase(phase_profile_, "run_preview_host");
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
        const bool runtime_reference =
            preview_config_.runtime_mode == TerrainPreviewRuntimeMode::TerrainEngineReference;
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path(runtime_reference
                                                              ? "terrain_engine_runtime.vert.spv"
                                                              : "terrain_preview.vert.spv")),
            cubey::render::fragment_shader_file(shader_path(runtime_reference
                                                                ? "terrain_engine_runtime.frag.spv"
                                                                : "terrain_preview.frag.spv")),
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
                        .material_pass = terrain_preview_pass_info(),
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

    [[nodiscard]] TerrainPreviewPushConstants push_constants(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const TerrainPreviewCameraFrame frame =
            terrain_preview_camera_frame(preview_config_.camera_preset);
        const float min_height = scene_metrics_.min_height_m * preview_config_.vertical_scale;
        const float max_height = scene_metrics_.max_height_m * preview_config_.vertical_scale;
        const float target_y =
            min_height + ((max_height - min_height) * frame.target_height_fraction);
        const cubey::Transform3D camera_transform = cubey::orbit_camera_transform({
            .target = {0.0F, target_y, 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw() + frame.yaw_radians,
            .pitch = orbit_controller_.pitch() + frame.pitch_radians,
        });
        return {
            .view_projection = camera_.view_projection_matrix(camera_transform, aspect),
            .light_direction_extent =
                {
                    0.38F,
                    0.82F,
                    0.42F,
                    scene_metrics_.scene_extent_m,
                },
            .terrain_runtime_params =
                {
                    terrain_engine_reference_seed_components(preview_config_.region.seed).x,
                    terrain_engine_reference_seed_components(preview_config_.region.seed).y,
                    preview_config_.vertical_scale,
                    preview_config_.water_surface ? 1.0F : 0.0F,
                },
            .water_params =
                {
                    kTerrainEngineReferenceWaterHeightM,
                    0.0F,
                    0.0F,
                    0.0F,
                },
        };
    }

    void record_frame(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                      cubey::render::ColorTargetView color_target,
                      cubey::render::FrameSlot frame_slot, bool present) {
        const TerrainPreviewPushConstants constants = push_constants(color_target.extent);
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
            "terrain preview backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("terrain preview depth", forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());

        graph.add_pass("terrain preview scene", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(backbuffer)
            .write_depth(depth)
            .material_pass(terrain_preview_pass_info())
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
                .label = "vkEndCommandBuffer terrain_preview",
                .command_buffer_mode =
                    present ? cubey::render::RenderGraphCommandBufferMode::BeginAndEnd
                            : cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            frame_graph);
    }

    [[nodiscard]] const cubey::render::Mesh& mesh() const {
        if (!mesh_.has_value()) {
            throw std::runtime_error("terrain preview mesh is not initialized");
        }
        return mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("terrain preview forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    RunConfig config_;
    TerrainPreviewConfig preview_config_{};
    TerrainPhaseProfile::TimePoint total_start_{};
    TerrainPhaseProfile phase_profile_{};
    std::optional<TerrainRegionProduct> product_{};
    TerrainPreviewMeshData mesh_data_{};
    TerrainPreviewSceneMetrics scene_metrics_{};
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_;
    std::optional<cubey::render::Mesh> mesh_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
};

} // namespace

int run_terrain_preview(const cubey::RunConfig& config) {
    TerrainPreviewApp app(config);
    return app.run();
}

} // namespace cubey::projects::terrain
