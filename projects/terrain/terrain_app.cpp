#include "terrain_app.h"

#include "terrain_backdrop_material.h"
#include "terrain_backdrop_placement.h"
#include "terrain_backdrop_product.h"
#include "terrain_config.h"
#include "terrain_environment_gpu.h"
#include "terrain_raster_height_source.h"
#include "terrain_shadow.h"

#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/cloud_environment_runtime.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/cloud_environment_ui.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/imgui_helpers.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/cloud_layer.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/shadow_map.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/vk_check.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef CUBEY_TERRAIN_SHADER_DIR
#error "CUBEY_TERRAIN_SHADER_DIR must be defined by the terrain CMake target"
#endif

#ifndef CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD
#error "CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD must be defined by the terrain CMake target"
#endif

namespace cubey::projects::terrain {
namespace {

constexpr VkFormat kTerrainSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr float kTerrainHeadlessOrbitSpeed = 0.18F;
constexpr std::uint32_t kTerrainGpuProfilerPassCapacity = 16U;
constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
constexpr float kTerrainMinimumForegroundHeightM = 2.0F;
constexpr float kTerrainDefaultForegroundHeightM = 100.0F;
constexpr float kTerrainMaximumForegroundHeightM = 1'000.0F;
constexpr float kTerrainDefaultTimeHours = 9.0F;
constexpr float kTerrainCloudSceneDepthFadeM = 500.0F;
constexpr float kTerrainInspectionPitchLimitRadians = 1'000.0F;
constexpr float kTerrainInspectionMaximumOrbitRadiusM = 1'000.0F;

constexpr std::array<std::string_view, 8U> kTerrainGpuTimingLabels{
    "terrain shadow", "terrain atmosphere", "terrain surface", "terrain stage proxy",
    "cloud march",    "cloud temporal",     "cloud composite", "terrain post",
};

struct TerrainStageProxyPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 camera_position{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 object_translation{0.0F, 0.0F, 0.0F, 0.0F};
};

struct TerrainBackdropPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 camera_position{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 render_options{0.0F, 0.0F, 1.0F, 3'200.0F};
    cubey::math::Vec4 material_options{1.0F, 0.0F, 0.0F, 0.0F};
};

struct TerrainShadowPushConstants {
    cubey::math::Mat4 light_view_projection{1.0F};
};

struct TerrainBackdropDrawPlan {
    bool center_visible = false;
    std::vector<bool> visible_sectors{};
    std::uint32_t submitted_sector_count = 0U;
    std::uint32_t submitted_triangle_count = 0U;
};

struct TerrainPlacementBuild {
    TerrainPlacementMode mode = TerrainPlacementMode::Selected;
    std::uint32_t sample_index = 0U;
    TerrainBackdropPlacementPlan placement{};
    TerrainBackdropProduct product{};
};

struct CompiledTerrainGraph {
    cubey::render::CompiledRenderGraph graph{};
    cubey::render::RenderGraphTextureHandle scene_color{};
    cubey::render::RenderGraphTextureHandle post_scene_color{};
    cubey::render::RenderGraphTextureHandle scene_depth{};
    cubey::render::CloudLayerRuntimeFrame cloud{};
    bool clouds_enabled = false;
};

static_assert(sizeof(TerrainStageProxyPushConstants) <= 128U);
static_assert(sizeof(TerrainBackdropPushConstants) <= 128U);
static_assert(sizeof(TerrainShadowPushConstants) <= 128U);

[[nodiscard]] std::uint64_t profile_frame_index(std::uint64_t frame_index) {
    return frame_index == 0U ? 0U : frame_index - 1U;
}

[[nodiscard]] std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                                          cubey::render::FrameSlot frame_slot) {
    if (frame_index > frame_slot.count) {
        return frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame_index);
}

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings) {
    if (recorder == nullptr) {
        return;
    }
    for (const cubey::vulkan::GpuPassTiming& timing : timings) {
        recorder->record_gpu_span(frame_index, timing.label, timing.milliseconds);
    }
}

void draw_terrain_gpu_timings(std::span<const cubey::vulkan::GpuPassTiming> timings) {
    ImGui::SeparatorText("GPU timings");
    for (const std::string_view label : kTerrainGpuTimingLabels) {
        const auto timing = std::find_if(timings.begin(), timings.end(),
                                         [label](const cubey::vulkan::GpuPassTiming& candidate) {
                                             return candidate.label == label;
                                         });
        if (timing == timings.end()) {
            ImGui::Text("%.*s: --", static_cast<int>(label.size()), label.data());
        } else {
            ImGui::Text("%s: %.3f ms", timing->label.c_str(), timing->milliseconds);
        }
    }
}

[[nodiscard]] std::filesystem::path shader_path(std::filesystem::path filename) {
    return std::filesystem::path(CUBEY_TERRAIN_SHADER_DIR) / std::move(filename);
}

[[nodiscard]] cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files() {
    return cubey::render::cloud_layer_runtime_shader_files(
        CUBEY_TERRAIN_SHADER_DIR,
        cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
}

[[nodiscard]] std::filesystem::path
require_heightfield(const std::filesystem::path& heightfield_path) {
    std::error_code error;
    if (!std::filesystem::exists(heightfield_path, error) || error) {
        throw std::runtime_error(
            "terrain heightfield asset is missing: " + heightfield_path.string() +
            "\nGenerate the canonical asset with:\n  cmake --build --preset dev --target "
            "cubey_terrain_generate_default_asset");
    }
    return heightfield_path;
}

[[nodiscard]] TerrainBackdropPlacementPlan
make_placement_stage(const TerrainRasterHeightSource& source, const TerrainRuntimeConfig& config) {
    if (config.expected_seed.has_value() &&
        config.expected_seed.value() != source.metadata().seed) {
        throw std::runtime_error("terrain seed does not match the heightfield manifest");
    }
    TerrainBackdropPlacementRequest request;
    request.mode = config.placement;
    request.sample_index = config.placement_index;
    return plan_terrain_backdrop_placement(source, source.bounds(), request);
}

[[nodiscard]] TerrainBackdropProduct make_product(const TerrainRasterHeightSource& source,
                                                  const TerrainBackdropPlacementPlan& placement) {
    return make_terrain_backdrop_product(
        {
            .source_focus_xz = placement.stage.source_focus_xz,
            .density = TerrainBackdropMeshDensity::High,
            .center_mode = TerrainBackdropCenterMode::Continuous,
            .center_sampling = TerrainBackdropCenterSampling::SeamMatched,
            .render_stride = 3U,
            .consumer_radius_m = placement.stage.stage_radius_m,
            .visible_inner_radius_m = 3'200.0F,
            .outer_radius_m = 16'384.0F,
            .vertical_scale = 1.0F,
            .vertical_offset_m = placement.stage.terrain_vertical_offset_m,
        },
        source);
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
        atmosphere.time_of_day_mode = "solar";
        atmosphere.time_hours = kTerrainDefaultTimeHours;
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

[[nodiscard]] cubey::CloudEnvironmentConfig
terrain_cloud_config(const RunConfig& config,
                     const cubey::render::AtmosphereEnvironmentConfig& atmosphere) {
    cubey::CloudEnvironmentConfig clouds{};
    cubey::apply_cloud_environment_weather_preset(
        clouds, cubey::CloudEnvironmentWeatherPreset::FairWeather);
    cubey::apply_cloud_environment_run_config(clouds, config.clouds);
    cubey::apply_cloud_environment_surface_v1_policy(clouds);
    clouds.layer.planet_radius_m = atmosphere.bottom_radius_km * 1000.0F;
    clouds.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    clouds.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Local;
    return clouds;
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_backdrop_pass_info() {
    constexpr VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    return {
        .label = "terrain.backdrop",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
                cubey::render::sampled_texture_descriptor_set_layout(1, 2U),
            },
        .push_constants =
            {
                VkPushConstantRange{
                    .stageFlags = stages,
                    .offset = 0,
                    .size = sizeof(TerrainBackdropPushConstants),
                },
            },
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo terrain_stage_proxy_pass_info() {
    return {
        .label = "terrain.stage-proxy",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .push_constants =
            {
                VkPushConstantRange{
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    .offset = 0,
                    .size = sizeof(TerrainStageProxyPushConstants),
                },
            },
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>
terrain_stage_proxy_mesh_data() {
    const auto sphere = cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
        .radius = 20.0F,
        .latitude_segments = 24U,
        .longitude_segments = 48U,
        .color = {0.52F, 0.55F, 0.58F},
    });
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal> result;
    result.vertices.reserve(sphere.vertices.size());
    for (const cubey::render::VertexPositionColorNormalUv& vertex : sphere.vertices) {
        result.vertices.push_back({
            .position = vertex.position,
            .color = vertex.color,
            .normal = vertex.normal,
        });
    }
    result.indices = sphere.indices;
    return result;
}

[[nodiscard]] std::string_view short_revision(std::string_view revision) {
    return revision.substr(0U, std::min<std::size_t>(revision.size(), 12U));
}

class TerrainApp {
  public:
    explicit TerrainApp(RunConfig config)
        : run_config_(std::move(config)),
          runtime_config_(terrain_runtime_config_from_run_config(
              run_config_, std::filesystem::path(CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD))),
          source_(require_heightfield(runtime_config_.heightfield_path)),
          placement_stage_(make_placement_stage(source_, runtime_config_)),
          product_(make_product(source_, placement_stage_)),
          orbit_controller_(cubey::OrbitControllerConfig{
              .min_pitch = -kTerrainInspectionPitchLimitRadians,
              .max_pitch = kTerrainInspectionPitchLimitRadians,
          }),
          camera_(cubey::Camera3DConfig{
              .fovy_radians = 40.0F * kDegreesToRadians,
              .near_z = 0.1F,
              .far_z = product_.request.outer_radius_m * 5.0F,
          }),
          atmosphere_state_(terrain_atmosphere_state(run_config_)),
          clouds_config_(terrain_cloud_config(run_config_, atmosphere_state_.environment)) {
        edit_placement_mode_ = runtime_config_.placement;
        edit_placement_index_ = runtime_config_.placement_index;
        configure_camera_for_placement(true);
    }

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
            maybe_install_placement_build(context);
            orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
            (void)cubey::atmosphere_environment_advance_time(atmosphere_state_,
                                                             timing.delta_seconds);
            cloud_runtime_.advance(timing.delta_seconds);
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            collect_gpu_timings(context.profile_recorder(), frame.timing.frame_index,
                                frame.frame_slot);
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
                .triangles = latest_draw_plan_.submitted_triangle_count,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        return cubey::host::run_windowed_app(
            {
                .run_config = run_config_,
                .app_name = "terrain",
                .ready_status = "rendering raster terrain backdrop",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = run_config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
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
            const float duration_seconds =
                run_config_.frames > 1U && run_config_.fps > 0U
                    ? static_cast<float>(run_config_.frames) / static_cast<float>(run_config_.fps)
                    : 0.0F;
            orbit_controller_.set_auto_rotation_speed(
                duration_seconds > 0.0F ? 2.0F * std::numbers::pi_v<float> / duration_seconds
                                        : kTerrainHeadlessOrbitSpeed);
            callbacks.before_frame = [this](cubey::host::HeadlessPngContext&,
                                            const cubey::host::HeadlessCaptureFrame& frame) {
                orbit_controller_.update(frame.timing.delta_seconds);
                (void)cubey::atmosphere_environment_advance_time(atmosphere_state_,
                                                                 frame.timing.delta_seconds);
                cloud_runtime_.advance(frame.timing.delta_seconds);
            };
        }
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            collect_gpu_timings(context.profile_recorder(), frame.timing.frame_index,
                                frame.frame_slot);
            record_target(context.device(), command_buffer, target, frame.frame_slot,
                          cubey::render::render_graph_color_attachment_texture_state(),
                          cubey::render::render_graph_color_attachment_texture_state(),
                          cubey::render::RenderGraphCommandBufferMode::AlreadyRecording);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        if (!cubey::host::begin_control_panel("Terrain", {.width = 410.0F})) {
            ImGui::End();
            return;
        }

        const TerrainRasterProvenance& provenance = source_.provenance();
        const TerrainHeightSourceMetadata metadata = source_.metadata();
        ImGui::SeparatorText("Source");
        ImGui::Text("%.*s, seed %llu", static_cast<int>(metadata.id.size()), metadata.id.data(),
                    static_cast<unsigned long long>(metadata.seed));
        ImGui::Text("%u x %u at %.0f m", source_.width(), source_.height(),
                    source_.sample_spacing_m());
        const float width_km =
            static_cast<float>(source_.width() - 1U) * source_.sample_spacing_m() * 0.001F;
        const float height_km =
            static_cast<float>(source_.height() - 1U) * source_.sample_spacing_m() * 0.001F;
        ImGui::Text("Coverage: %.2f x %.2f km", width_km, height_km);
        if (!provenance.generator.empty()) {
            ImGui::Text("Generator: %s", provenance.generator.c_str());
        }
        if (!provenance.model_id.empty()) {
            ImGui::TextWrapped("Model: %s", provenance.model_id.c_str());
        }
        if (!provenance.code_revision.empty() || !provenance.model_revision.empty()) {
            const std::string_view code = short_revision(provenance.code_revision);
            const std::string_view model = short_revision(provenance.model_revision);
            ImGui::Text("Revisions: %.*s / %.*s", static_cast<int>(code.size()), code.data(),
                        static_cast<int>(model.size()), model.data());
        }
        const std::string manifest = provenance.manifest_path.string();
        ImGui::TextWrapped("Manifest: %s", manifest.c_str());

        ImGui::SeparatorText("Placement");
        const bool placement_build_pending = placement_build_future_.valid();
        ImGui::BeginDisabled(placement_build_pending);
        if (ImGui::RadioButton("Selected",
                               edit_placement_mode_ == TerrainPlacementMode::Selected)) {
            edit_placement_mode_ = TerrainPlacementMode::Selected;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Raw center",
                               edit_placement_mode_ == TerrainPlacementMode::RawCenter)) {
            edit_placement_mode_ = TerrainPlacementMode::RawCenter;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Raw sample",
                               edit_placement_mode_ == TerrainPlacementMode::RawSample)) {
            edit_placement_mode_ = TerrainPlacementMode::RawSample;
        }
        if (edit_placement_mode_ == TerrainPlacementMode::RawSample) {
            constexpr std::uint32_t step = 1U;
            constexpr std::uint32_t fast_step = 10U;
            ImGui::InputScalar("Sample index", ImGuiDataType_U32, &edit_placement_index_, &step,
                               &fast_step);
        }
        ImGui::EndDisabled();

        const bool placement_edit_dirty = edit_placement_mode_ != placement_stage_.mode ||
                                          edit_placement_index_ != placement_stage_.sample_index;
        ImGui::BeginDisabled(!placement_edit_dirty || placement_build_pending);
        if (ImGui::Button("Apply placement")) {
            start_placement_build();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!placement_edit_dirty || placement_build_pending);
        if (ImGui::Button("Revert")) {
            edit_placement_mode_ = placement_stage_.mode;
            edit_placement_index_ = placement_stage_.sample_index;
        }
        ImGui::EndDisabled();

        if (placement_build_pending) {
            ImGui::Text("Status: rebuilding cached terrain...");
        }
        if (!placement_rebuild_error_.empty()) {
            ImGui::TextWrapped("Placement error: %s", placement_rebuild_error_.c_str());
        }

        const TerrainDirectionalPlacementPlan& placement = placement_stage_.placement;
        const std::string_view placement_name = terrain_placement_mode_name(placement_stage_.mode);
        ImGui::Text("Active: %.*s", static_cast<int>(placement_name.size()), placement_name.data());
        if (placement_stage_.mode == TerrainPlacementMode::RawSample) {
            ImGui::Text("Active sample: %u", placement_stage_.sample_index);
        }
        ImGui::Text("Focus: %.3f, %.3f km", placement.source_focus_xz.x * 0.001F,
                    placement.source_focus_xz.y * 0.001F);
        ImGui::Text("Directional contract: %s", placement.contract_satisfied ? "pass" : "fail");
        ImGui::Text("Score: %.3f", placement.score);
        ImGui::Text("Local relief: %.1f m", placement.local_relief_m);
        ImGui::Text("P95 slope: %.3f", placement.local_p95_slope);
        ImGui::Text("Mountain/open sectors: %u / %u", placement.mountain_sector_count,
                    placement.open_sector_count);
        ImGui::Text("Mountain/open arcs: %u / %u", placement.largest_mountain_arc_sectors,
                    placement.largest_open_arc_sectors);
        ImGui::Text("Baked clearance: %.1f m", placement_stage_.stage.minimum_camera_clearance_m);

        ImGui::SeparatorText("Camera");
        float radius = orbit_controller_.distance();
        const TerrainBackdropStagePlan& stage = placement_stage_.stage;
        if (ImGui::SliderFloat("Orbit radius", &radius, stage.orbit_min_radius_m,
                               kTerrainInspectionMaximumOrbitRadiusM, "%.0f m",
                               ImGuiSliderFlags_Logarithmic)) {
            orbit_controller_.set_distance(radius);
        }
        float elevation_degrees =
            (stage.orbit_default_elevation_radians - orbit_controller_.pitch()) * kRadiansToDegrees;
        if (ImGui::DragFloat("Elevation", &elevation_degrees, 0.25F, 0.0F, 0.0F, "%.1f deg")) {
            orbit_controller_.set_pitch(stage.orbit_default_elevation_radians -
                                        elevation_degrees * kDegreesToRadians);
        }
        const float maximum_foreground_height_m =
            std::max(kTerrainMaximumForegroundHeightM, baked_foreground_height_m_);
        ImGui::SliderFloat("Foreground height", &foreground_height_m_,
                           kTerrainMinimumForegroundHeightM, maximum_foreground_height_m, "%.0f m",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button("Reset Camera")) {
            foreground_height_m_ = runtime_config_.initial_foreground_height_m;
            reset_inspection_camera();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Foreground sphere", &runtime_config_.foreground_sphere);

        ImGui::SeparatorText("Presentation");
        int material = static_cast<int>(runtime_config_.material);
        if (ImGui::RadioButton("Flat", material == static_cast<int>(TerrainMaterialMode::Flat))) {
            runtime_config_.material = TerrainMaterialMode::Flat;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Filtered detail",
                               material == static_cast<int>(TerrainMaterialMode::FilteredDetail))) {
            runtime_config_.material = TerrainMaterialMode::FilteredDetail;
        }
        ImGui::Checkbox("Directional shadows", &runtime_config_.shadows);

        constexpr std::array diagnostics{
            TerrainDebugView::Surface,
            TerrainDebugView::Height,
            TerrainDebugView::Slope,
            TerrainDebugView::Clay,
            TerrainDebugView::Normal,
            TerrainDebugView::ClassificationNormal,
            TerrainDebugView::MaterialWeights,
            TerrainDebugView::AmbientVisibility,
            TerrainDebugView::MaterialAlbedo,
            TerrainDebugView::MaterialNormal,
            TerrainDebugView::MaterialRoughness,
            TerrainDebugView::SunVisibility,
            TerrainDebugView::ProjectedEdge,
            TerrainDebugView::StageOwnership,
        };
        const std::string_view current_view = terrain_debug_view_name(runtime_config_.debug_view);
        if (ImGui::BeginCombo("Diagnostic", current_view.data())) {
            for (const TerrainDebugView view : diagnostics) {
                const std::string_view name = terrain_debug_view_name(view);
                const bool selected = view == runtime_config_.debug_view;
                if (ImGui::Selectable(name.data(), selected)) {
                    runtime_config_.debug_view = view;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SeparatorText("Renderer");
        ImGui::Text("Submitted sectors: %u", latest_draw_plan_.submitted_sector_count);
        ImGui::Text("Submitted triangles: %u", latest_draw_plan_.submitted_triangle_count);
        ImGui::Text("Cached source samples: %llu",
                    static_cast<unsigned long long>(product_.diagnostics.source_sample_count));
        ImGui::Text("Shadow map: %u x %u, %s", kTerrainShadowMapExtent,
                    kTerrainShadowMapExtent, shadow_cache_.valid ? "valid" : "pending");
        ImGui::Text("Shadow casters: %llu triangles (outer backdrop)",
                    static_cast<unsigned long long>(shadow_caster_triangle_count()));
        ImGui::Text("Shadow updates: %llu",
                    static_cast<unsigned long long>(shadow_cache_.update_count));
        if (!frame_primary_light_above_horizon_) {
            ImGui::Text("Shadow update suspended below horizon");
        }
        draw_terrain_gpu_timings(latest_gpu_timings());

        if (cubey::host::draw_atmosphere_environment_controls(atmosphere_state_,
                                                              {.default_open = true})) {
            cubey::atmosphere_environment_resolve_run_state(atmosphere_state_);
        }
        if (cubey::host::draw_cloud_environment_controls(
                clouds_config_,
                {.default_open = false,
                 .help = "Shared Cloud V1 layer composited behind the terrain and foreground.",
                 .enabled_help = "Composite clouds into the terrain environment.",
                 .scene_depth_occlusion_enabled = true,
                 .scene_depth_fade_m = kTerrainCloudSceneDepthFadeM})) {
            clouds_config_.layer.planet_radius_m =
                atmosphere_state_.environment.bottom_radius_km * 1000.0F;
            cloud_runtime_.set_config(clouds_config_);
            cloud_runtime_.update_weather_texture(context.device(), context.gpu(),
                                                  cloud_runtime_shader_files().generated.weather);
        }
        ImGui::End();
    }

    void configure_camera_for_placement(bool reset_foreground_height) {
        const TerrainBackdropStagePlan& stage = placement_stage_.stage;
        orbit_controller_.set_distance_limits(stage.orbit_min_radius_m,
                                              kTerrainInspectionMaximumOrbitRadiusM);
        orbit_controller_.set_home_distance(
            runtime_config_.initial_orbit_radius_m.value_or(stage.orbit_default_radius_m));
        baked_foreground_height_m_ = stage.target_height_m - stage.source_center_height_m;
        if (reset_foreground_height) {
            foreground_height_m_ = runtime_config_.initial_foreground_height_m;
        }
        reset_inspection_camera();
    }

    void reset_inspection_camera() {
        const TerrainBackdropStagePlan& stage = placement_stage_.stage;
        orbit_controller_.reset();
        if (runtime_config_.initial_elevation_radians.has_value()) {
            orbit_controller_.set_pitch(stage.orbit_default_elevation_radians -
                                        runtime_config_.initial_elevation_radians.value());
        }
    }

    void start_placement_build() {
        if (placement_build_future_.valid()) {
            return;
        }

        TerrainRuntimeConfig config = runtime_config_;
        config.placement = edit_placement_mode_;
        config.placement_index = edit_placement_index_;
        placement_rebuild_error_.clear();
        try {
            placement_build_future_ =
                std::async(std::launch::async, [this, config = std::move(config)] {
                    TerrainPlacementBuild build;
                    build.mode = config.placement;
                    build.sample_index = config.placement_index;
                    build.placement = make_placement_stage(source_, config);
                    build.product = make_product(source_, build.placement);
                    return build;
                });
        } catch (const std::exception& error) {
            placement_rebuild_error_ = error.what();
        } catch (...) {
            placement_rebuild_error_ = "unable to start terrain placement rebuild";
        }
    }

    void install_placement_build(cubey::host::WindowedAppContext& context,
                                 TerrainPlacementBuild build) {
        if (!global_resources_created_) {
            throw std::runtime_error("terrain resources are not ready for a placement rebuild");
        }

        std::optional<cubey::render::Mesh> next_center_mesh;
        if (build.product.center.has_value()) {
            next_center_mesh.emplace(context.gpu(), build.product.center->mesh_config());
        }
        std::vector<cubey::render::Mesh> next_sector_meshes;
        next_sector_meshes.reserve(build.product.sectors.size());
        for (const TerrainBackdropSectorMesh& sector : build.product.sectors) {
            next_sector_meshes.emplace_back(context.gpu(), sector.mesh_config());
        }
        static_cast<void>(context.gpu().drain());
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle terrain placement rebuild");

        center_mesh_ = std::move(next_center_mesh);
        sector_meshes_ = std::move(next_sector_meshes);
        placement_stage_ = std::move(build.placement);
        product_ = std::move(build.product);
        runtime_config_.placement = build.mode;
        runtime_config_.placement_index = build.sample_index;
        latest_draw_plan_ = {};
        invalidate_terrain_shadow_cache(shadow_cache_);
        configure_camera_for_placement(false);
    }

    void maybe_install_placement_build(cubey::host::WindowedAppContext& context) {
        if (!placement_build_future_.valid() ||
            placement_build_future_.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
            return;
        }

        try {
            install_placement_build(context, placement_build_future_.get());
            placement_rebuild_error_.clear();
        } catch (const std::exception& error) {
            placement_rebuild_error_ = error.what();
        } catch (...) {
            placement_rebuild_error_ = "unknown terrain placement rebuild error";
        }
    }

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (global_resources_created_) {
            return;
        }
        gpu_profiler_.emplace(device, frame_slot_count, kTerrainGpuProfilerPassCapacity);
        if (product_.center.has_value()) {
            center_mesh_.emplace(gpu, product_.center->mesh_config());
        }
        sector_meshes_.reserve(product_.sectors.size());
        for (const TerrainBackdropSectorMesh& sector : product_.sectors) {
            sector_meshes_.emplace_back(gpu, sector.mesh_config());
        }
        const auto stage_proxy_data = terrain_stage_proxy_mesh_data();
        stage_proxy_mesh_.emplace(gpu, stage_proxy_data.mesh_config());

        const VkPushConstantRange shadow_push_constants{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0U,
            .size = sizeof(TerrainShadowPushConstants),
        };
        const std::array shadow_shaders{
            cubey::render::vertex_shader_file(shader_path("terrain_shadow_depth.vert.spv")),
        };
        const cubey::render::VertexInputLayout shadow_vertex_input =
            cubey::render::vertex_position_only_input_layout(
                sizeof(cubey::render::VertexPositionColorNormal));
        shadow_pass_.emplace(
            device,
            cubey::render::ShadowMapPass3DConfig{
                .extent = {kTerrainShadowMapExtent, kTerrainShadowMapExtent},
                .pipeline =
                    {
                        .shader_stage_files = shadow_shaders,
                        .vertex_bindings = shadow_vertex_input.bindings(),
                        .vertex_attributes = shadow_vertex_input.attribute_descriptions(),
                        .material_pass = cubey::render::shadow_depth_pass_info({
                            .label = "terrain.shadow",
                            .push_constants = std::span<const VkPushConstantRange>{
                                &shadow_push_constants, 1U},
                            .cull_mode = VK_CULL_MODE_NONE,
                        }),
                    },
            });
        invalidate_terrain_shadow_cache(shadow_cache_);
        shadow_depth_is_sampled_ = false;

        material_texture_.emplace(create_terrain_backdrop_material_texture(
            device, gpu, shader_path("terrain_backdrop_material.comp.spv"),
            source_.metadata().seed));
        detail_material_.emplace(device, cubey::render::MaterialInstanceConfig{
                                             .material_pass = terrain_backdrop_pass_info(),
                                             .descriptor_set = 1,
                                         });
        cubey::render::MaterialDescriptorWriter detail_writer(detail_material_->set());
        const cubey::render::Texture2D& detail = material_texture_->detail;
        detail_writer.combined_image_sampler(0U, detail.sampler().handle(), detail.view());
        const cubey::render::DepthTexture& shadow = shadow_pass().depth_texture();
        detail_writer.combined_image_sampler(1U, shadow.sampler().handle(), shadow.view(),
                                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        detail_writer.update(device);

        environment_material_.emplace(device, cubey::render::FrameUniformMaterialInstanceConfig{
                                                  .material_pass = terrain_backdrop_pass_info(),
                                                  .descriptor_set = 0,
                                                  .frame_slot_count = frame_slot_count,
                                                  .uniform_binding = 0,
                                              });
        atmosphere_atlases_.emplace(cubey::render::create_atmosphere_background_generated_textures(
            device, gpu, {.night_sky_extent = 64U}));
        atmosphere_background_.create_materials(
            device,
            {.frame_slot_count = frame_slot_count, .textures = atmosphere_atlases_->bindings()});
        cloud_runtime_.create_surface_resources(device, gpu, cloud_runtime_shader_files().generated,
                                                clouds_config_);
        hdr_post_frame_.create_materials(device, {.frame_slot_count = frame_slot_count});
        global_resources_created_ = true;
    }

    void create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                    VkFormat color_format, std::uint32_t frame_slot_count) {
        const std::array terrain_shaders{
            cubey::render::vertex_shader_file(shader_path("terrain_backdrop.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("terrain_backdrop_detail.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input =
            cubey::render::vertex_position_color_normal_input_layout();
        const std::array terrain_descriptor_set_layouts{
            environment_material().layout(),
            detail_material().layout(),
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
                        .material_pass = terrain_backdrop_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
                .sampled_depth = true,
            });

        const std::array stage_proxy_shaders{
            cubey::render::vertex_shader_file(shader_path("terrain_stage_proxy.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("terrain_stage_proxy.frag.spv")),
        };
        const std::array stage_proxy_descriptor_set_layouts{environment_material().layout()};
        stage_proxy_pipeline_.emplace(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = extent,
                        .color_format = kTerrainSceneColorFormat,
                        .depth_format = terrain_forward_pass().depth_target().format,
                        .shader_stage_files = stage_proxy_shaders,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .descriptor_set_layouts = stage_proxy_descriptor_set_layouts,
                        .material_pass = terrain_stage_proxy_pass_info(),
                    });

        const std::array atmosphere_shaders{
            cubey::render::vertex_shader_file(shader_path("atmosphere.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("atmosphere.frag.spv")),
        };
        atmosphere_background_.create_pipeline(device, {.extent = extent,
                                                        .color_format = kTerrainSceneColorFormat,
                                                        .shader_stage_files = atmosphere_shaders});
        cloud_runtime_.create_surface_target_resources(
            device, cloud_runtime_shader_files(),
            cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
            kTerrainSceneColorFormat, extent, frame_slot_count);
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
        cloud_runtime_.destroy_surface_target_resources();
        atmosphere_background_.destroy_pipeline();
        stage_proxy_pipeline_.reset();
        terrain_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        hdr_post_frame_.destroy();
        cloud_runtime_.destroy();
        atmosphere_background_.destroy();
        atmosphere_atlases_.reset();
        detail_material_.reset();
        material_texture_.reset();
        shadow_pass_.reset();
        environment_material_.reset();
        stage_proxy_mesh_.reset();
        center_mesh_.reset();
        sector_meshes_.clear();
        gpu_profiler_.reset();
        invalidate_terrain_shadow_cache(shadow_cache_);
        shadow_depth_is_sampled_ = false;
        global_resources_created_ = false;
    }

    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_gpu_timings() const {
        if (!gpu_profiler_.has_value()) {
            static const std::vector<cubey::vulkan::GpuPassTiming> empty;
            return empty;
        }
        return gpu_profiler_->latest_timings();
    }

    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* gpu_profiler() {
        return gpu_profiler_.has_value() ? &gpu_profiler_.value() : nullptr;
    }

    void collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                             std::uint64_t frame_index, cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler();
        if (profiler == nullptr) {
            return;
        }
        profiler->collect(frame_slot.index);
        const std::uint64_t collected_frame =
            collected_profile_frame_index(frame_index, frame_slot);
        record_gpu_timings(profile_recorder, collected_frame, latest_gpu_timings());
        record_metrics(profile_recorder, collected_frame);
    }

    [[nodiscard]] std::uint64_t shadow_caster_triangle_count() const noexcept {
        return product_.diagnostics.render_triangle_count -
               product_.diagnostics.center_render_triangle_count;
    }

    void record_metrics(cubey::profiling::ProfileRecorder* recorder,
                        std::uint64_t frame_index) const {
        if (recorder == nullptr) {
            return;
        }
        recorder->record_metric(frame_index, "terrain.backdrop", "submitted_sectors",
                                latest_draw_plan_.submitted_sector_count);
        recorder->record_metric(frame_index, "terrain.backdrop", "submitted_triangles",
                                latest_draw_plan_.submitted_triangle_count);
        recorder->record_metric(frame_index, "terrain.backdrop", "product_render_triangles",
                                static_cast<double>(product_.diagnostics.render_triangle_count));
        recorder->record_metric(frame_index, "terrain.backdrop", "source_samples",
                                static_cast<double>(product_.diagnostics.source_sample_count));
        recorder->record_metric(
            frame_index, "terrain.backdrop", "content_hash_low32",
            static_cast<double>(static_cast<std::uint32_t>(product_.diagnostics.content_hash)));
        recorder->record_metric(
            frame_index, "terrain.backdrop", "content_hash_high32",
            static_cast<double>(static_cast<std::uint32_t>(product_.diagnostics.content_hash >>
                                                           32U)));
        recorder->record_metric(frame_index, "terrain.backdrop", "render_stride", 3.0);
        recorder->record_metric(frame_index, "terrain.backdrop", "outer_radius_m",
                                product_.request.outer_radius_m);
        recorder->record_metric(frame_index, "terrain.placement", "mode",
                                static_cast<double>(placement_stage_.mode));
        recorder->record_metric(frame_index, "terrain.placement", "sample_index",
                                static_cast<double>(placement_stage_.sample_index));
        recorder->record_metric(frame_index, "terrain.placement", "source_focus_x_m",
                                placement_stage_.placement.source_focus_xz.x);
        recorder->record_metric(frame_index, "terrain.placement", "source_focus_z_m",
                                placement_stage_.placement.source_focus_xz.y);
        recorder->record_metric(frame_index, "terrain.placement", "directional_contract",
                                placement_stage_.placement.contract_satisfied ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.placement", "score",
                                placement_stage_.placement.score);
        recorder->record_metric(frame_index, "terrain.placement", "local_relief_m",
                                placement_stage_.placement.local_relief_m);
        recorder->record_metric(frame_index, "terrain.placement", "local_p95_slope",
                                placement_stage_.placement.local_p95_slope);
        recorder->record_metric(frame_index, "terrain.placement", "baked_clearance_m",
                                placement_stage_.stage.minimum_camera_clearance_m);
        recorder->record_metric(frame_index, "terrain.placement", "foreground_height_m",
                                foreground_height_m_);
        recorder->record_metric(
            frame_index, "terrain.backdrop", "filtered_detail",
            runtime_config_.material == TerrainMaterialMode::FilteredDetail ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.backdrop", "material_texture_bytes",
                                static_cast<double>(terrain_backdrop_material_texture_bytes()));
        recorder->record_metric(frame_index, "terrain.shadow", "enabled",
                                runtime_config_.shadows ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.shadow", "map_extent",
                                static_cast<double>(kTerrainShadowMapExtent));
        recorder->record_metric(frame_index, "terrain.shadow", "triangle_count",
                                static_cast<double>(shadow_caster_triangle_count()));
        recorder->record_metric(frame_index, "terrain.shadow", "cache_valid",
                                shadow_cache_.valid ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.shadow", "update_count",
                                static_cast<double>(shadow_cache_.update_count));
        recorder->record_metric(frame_index, "terrain.shadow", "updated",
                                shadow_updated_last_frame_ ? 1.0 : 0.0);
    }

    [[nodiscard]] TerrainBackdropDrawPlan draw_plan(VkExtent2D extent) const {
        TerrainBackdropDrawPlan plan;
        if (product_.center.has_value()) {
            plan.center_visible = true;
            plan.submitted_triangle_count += product_.center->triangle_count();
        }
        plan.visible_sectors.resize(product_.sectors.size());
        const cubey::math::Mat4 view_projection =
            camera_.view_projection_matrix(frame_camera_transform_, aspect(extent));
        const cubey::scene::Frustum3D frustum =
            cubey::scene::frustum_from_view_projection(view_projection);
        const cubey::math::Vec2 view_direction{-frame_camera_transform_.translation.x,
                                               -frame_camera_transform_.translation.z};
        const float view_distance =
            std::sqrt(view_direction.x * view_direction.x + view_direction.y * view_direction.y);
        const float view_azimuth = std::atan2(view_direction.x, -view_direction.y);
        const float horizontal_half_fov =
            std::atan(std::tan(camera_.fovy_radians() * 0.5F) * aspect(extent));
        const float parallax_guard =
            std::asin(
                std::clamp(view_distance / product_.request.visible_inner_radius_m, 0.0F, 0.5F)) +
            kDegreesToRadians;
        for (std::size_t index = 0U; index < product_.sectors.size(); ++index) {
            const TerrainBackdropSectorMesh& sector = product_.sectors[index];
            const TerrainBackdropSectorBounds& sector_bounds = sector.bounds;
            const cubey::Bounds3D bounds{
                .center = sector_bounds.center,
                .half_extent = {(sector_bounds.maximum.x - sector_bounds.minimum.x) * 0.5F,
                                (sector_bounds.maximum.y - sector_bounds.minimum.y) * 0.5F,
                                (sector_bounds.maximum.z - sector_bounds.minimum.z) * 0.5F},
            };
            const float sector_center =
                (sector.begin_azimuth_radians + sector.end_azimuth_radians) * 0.5F;
            const float sector_half_width =
                (sector.end_azimuth_radians - sector.begin_azimuth_radians) * 0.5F;
            const float angular_delta = std::abs(
                std::remainder(sector_center - view_azimuth, 2.0F * std::numbers::pi_v<float>));
            const bool visible =
                angular_delta <= horizontal_half_fov + sector_half_width + parallax_guard &&
                cubey::scene::intersects(frustum, bounds);
            plan.visible_sectors[index] = visible;
            if (visible) {
                ++plan.submitted_sector_count;
                plan.submitted_triangle_count += sector.triangle_count();
            }
        }
        return plan;
    }

    [[nodiscard]] cubey::Transform3D current_camera_transform() const {
        const TerrainBackdropStagePlan& stage = placement_stage_.stage;
        const float initial_yaw =
            runtime_config_.initial_azimuth_radians.value_or(stage.showcase_yaw_radians);
        return cubey::orbit_camera_transform({
            .target = {0.0F, foreground_vertical_offset_m(), 0.0F},
            .distance = orbit_controller_.distance(),
            .yaw = orbit_controller_.yaw() + initial_yaw,
            .pitch = orbit_controller_.pitch() - stage.orbit_default_elevation_radians,
        });
    }

    [[nodiscard]] float foreground_vertical_offset_m() const noexcept {
        return foreground_height_m_ - baked_foreground_height_m_;
    }

    [[nodiscard]] static float aspect(VkExtent2D extent) {
        return extent.height == 0U
                   ? 1.0F
                   : static_cast<float>(extent.width) / static_cast<float>(extent.height);
    }

    [[nodiscard]] TerrainBackdropPushConstants backdrop_push_constants(VkExtent2D extent) const {
        return {
            .view_projection =
                camera_.view_projection_matrix(frame_camera_transform_, aspect(extent)),
            .camera_position = {frame_camera_transform_.translation.x,
                                frame_camera_transform_.translation.y,
                                frame_camera_transform_.translation.z, 0.0F},
            .render_options = {static_cast<float>(
                                   static_cast<std::uint8_t>(runtime_config_.debug_view)),
                               product_.diagnostics.minimum_height_m,
                               product_.diagnostics.maximum_height_m,
                               product_.request.visible_inner_radius_m},
            .material_options = {runtime_config_.material == TerrainMaterialMode::FilteredDetail
                                     ? 1.0F
                                     : 0.0F,
                                 0.0F, 0.0F, 0.0F},
        };
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_uniforms(VkExtent2D extent) const {
        const float physical_camera_height_m =
            frame_camera_transform_.translation.y + placement_stage_.stage.target_height_m;
        return cubey::render::atmosphere_environment_frame_uniforms(
            atmosphere_state_.environment,
            {
                .view_rays = cubey::render::view_ray_basis_3d(
                    frame_camera_transform_.rotation, aspect(extent), camera_.fovy_radians()),
                .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
                .camera_position_km = {0.0F,
                                       atmosphere_state_.environment.bottom_radius_km +
                                           std::max(physical_camera_height_m, 0.0F) * 0.001F,
                                       0.0F},
                .camera_position_km_explicit = true,
            });
    }

    [[nodiscard]] bool cloud_layer_enabled() const noexcept {
        return runtime_config_.debug_view == TerrainDebugView::Surface && clouds_config_.enabled;
    }

    [[nodiscard]] cubey::CloudEnvironmentRuntimeFrame
    current_cloud_frame(VkExtent2D extent,
                        const cubey::render::AtmosphereEnvironmentLighting& lighting) const {
        const cubey::render::ViewRayBasis3D view_rays = cubey::render::view_ray_basis_3d(
            frame_camera_transform_.rotation, aspect(extent), camera_.fovy_radians());
        const float physical_camera_height_m =
            frame_camera_transform_.translation.y + placement_stage_.stage.target_height_m;
        return cloud_runtime_.frame(
            cubey::CloudEnvironmentSurfaceViewInfo{
                .camera_position = {frame_camera_transform_.translation.x,
                                    std::max(physical_camera_height_m, 0.0F),
                                    frame_camera_transform_.translation.z},
                .camera_right = cubey::math::Vec3{view_rays.right_aspect},
                .camera_up = cubey::math::Vec3{view_rays.up_tan_half_fovy},
                .camera_forward = cubey::math::Vec3{view_rays.forward},
                .tan_half_fovy = view_rays.up_tan_half_fovy.w,
                .target_extent = extent,
                .near_plane_m = camera_.near_z(),
                .far_plane_m = camera_.far_z(),
                .external_background = true,
                .scene_depth_mode = cubey::render::CloudLayerSceneDepthMode::OpaqueForeground,
                .scene_depth_fade_m = kTerrainCloudSceneDepthFadeM,
            },
            lighting);
    }

    [[nodiscard]] float display_exposure() const {
        return run_config_.pbr.exposure_explicit ? run_config_.pbr.exposure
                                                 : atmosphere_state_.resolved_exposure;
    }

    void record_shadow_pass(const cubey::vulkan::CommandRecorder& recorder) const {
        shadow_pass().record(
            recorder, cubey::render::depth_clear_value(),
            [this](const cubey::vulkan::CommandRecorder& pass_recorder) {
                pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            shadow_pass().pipeline().pipeline());
                pass_recorder.push_constants(
                    shadow_pass().pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0U,
                    TerrainShadowPushConstants{
                        .light_view_projection =
                            frame_shadow_projection_.light_view_projection,
                    });
                // The inner stage is a continuity receiver; its polar LOD rings are not casters.
                for (const cubey::render::Mesh& mesh : sector_meshes_) {
                    cubey::render::record_draw_item(
                        pass_recorder.handle(), cubey::render::DrawItem{.mesh = &mesh});
                }
            });
    }

    void record_terrain_pass(const cubey::vulkan::CommandRecorder& recorder,
                             cubey::render::ColorTargetView color,
                             cubey::render::DepthTargetView depth,
                             cubey::render::FrameSlot frame_slot) const {
        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(color, depth);
        const cubey::render::RenderTargetRenderingInfo rendering(
            target, terrain_forward_pass().clear_values(),
            cubey::render::RenderTargetAttachmentOps{
                .color = cubey::vulkan::load_store_attachment_ops(),
                .depth = cubey::vulkan::clear_discard_attachment_ops(),
            });
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(color.extent);
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                               terrain_forward_pass().pipeline().pipeline());
        cubey::render::bind_material_instance(recorder, terrain_forward_pass().pipeline(),
                                              environment_material().material(), frame_slot);
        cubey::render::bind_material_instance(recorder, terrain_forward_pass().pipeline(),
                                              detail_material());
        recorder.push_constants(terrain_forward_pass().pipeline().layout(),
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                backdrop_push_constants(color.extent));
        latest_draw_plan_ = draw_plan(color.extent);
        if (latest_draw_plan_.center_visible) {
            cubey::render::record_draw_item(recorder.handle(),
                                            cubey::render::DrawItem{.mesh = &center_mesh()});
        }
        for (std::size_t index = 0U; index < sector_meshes_.size(); ++index) {
            if (latest_draw_plan_.visible_sectors[index]) {
                cubey::render::record_draw_item(
                    recorder.handle(), cubey::render::DrawItem{.mesh = &sector_meshes_[index]});
            }
        }
        recorder.end_rendering();
    }

    void record_stage_proxy_pass(const cubey::vulkan::CommandRecorder& recorder,
                                 cubey::render::ColorTargetView color,
                                 cubey::render::DepthTargetView depth,
                                 cubey::render::FrameSlot frame_slot) const {
        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(color, depth);
        const cubey::render::RenderTargetRenderingInfo rendering(
            target, terrain_forward_pass().clear_values(),
            cubey::render::RenderTargetAttachmentOps{
                .color = cubey::vulkan::load_store_attachment_ops(),
                .depth = cubey::vulkan::load_store_attachment_ops(),
            });
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(color.extent);
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, stage_proxy_pipeline().pipeline());
        cubey::render::bind_material_instance(recorder, stage_proxy_pipeline(),
                                              environment_material().material(), frame_slot);
        recorder.push_constants(
            stage_proxy_pipeline().layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            TerrainStageProxyPushConstants{
                .view_projection =
                    camera_.view_projection_matrix(frame_camera_transform_, aspect(color.extent)),
                .camera_position = {frame_camera_transform_.translation.x,
                                    frame_camera_transform_.translation.y,
                                    frame_camera_transform_.translation.z, 0.0F},
                .object_translation = {0.0F, foreground_vertical_offset_m(), 0.0F, 0.0F},
            });
        cubey::render::record_draw_item(recorder.handle(),
                                        cubey::render::DrawItem{.mesh = &stage_proxy_mesh()});
        recorder.end_rendering();
    }

    [[nodiscard]] CompiledTerrainGraph current_render_graph(
        cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
        cubey::render::RenderGraphTextureState target_initial_state,
        cubey::render::RenderGraphTextureState target_final_state,
        const std::optional<cubey::CloudEnvironmentRuntimeFrame>& cloud_runtime_frame) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "terrain backbuffer", target, target_initial_state, target_final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "terrain scene color", target.extent, kTerrainSceneColorFormat));
        cubey::render::RenderGraphTextureHandle post_scene_color = scene_color;
        cubey::render::CloudLayerRuntimeFrame cloud_frame{};
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("terrain depth", terrain_forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());
        const std::optional<cubey::render::RenderGraphTextureState> shadow_initial_state =
            shadow_depth_is_sampled_
                ? cubey::render::render_graph_sampled_depth_texture_state()
                : cubey::render::render_graph_undefined_texture_state();
        const cubey::render::RenderGraphTextureHandle shadow_depth = graph.import_depth_target(
            "terrain shadow depth", shadow_pass().depth_target(), shadow_initial_state);

        if (shadow_update_this_frame_) {
            graph.add_pass("terrain shadow", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_depth(shadow_depth)
                .material_pass(shadow_pass().material_pass())
                .execute([this](const cubey::render::RenderGraphExecutionContext& context) {
                    record_shadow_pass(context.recorder());
                });
        }

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
            .read_texture(shadow_depth)
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(terrain_backdrop_pass_info())
            .execute([this, scene_color, depth,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_terrain_pass(context.recorder(),
                                    cubey::render::resolved_color_target_view(context, scene_color),
                                    cubey::render::resolved_depth_target_view(context, depth),
                                    frame_slot);
            });
        if (runtime_config_.foreground_sphere) {
            graph.add_pass("terrain stage proxy", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_color(scene_color)
                .write_depth(depth)
                .material_pass(terrain_stage_proxy_pass_info())
                .execute([this, scene_color, depth,
                          frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                    record_stage_proxy_pass(
                        context.recorder(),
                        cubey::render::resolved_color_target_view(context, scene_color),
                        cubey::render::resolved_depth_target_view(context, depth), frame_slot);
                });
        }
        const bool clouds_enabled = cloud_runtime_frame.has_value() && cloud_runtime_frame->enabled;
        if (clouds_enabled) {
            const cubey::render::RenderGraphTextureHandle cloud_scene_color =
                graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                    "terrain cloud scene color", target.extent, kTerrainSceneColorFormat));
            cloud_frame = cloud_runtime_.declare_surface_product(graph, frame_slot,
                                                                 cloud_runtime_frame.value());
            cloud_runtime_.declare_surface_composite(graph, cloud_scene_color, cloud_frame,
                                                     frame_slot, scene_color, depth);
            post_scene_color = cloud_scene_color;
        }
        graph.add_pass("terrain post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(post_scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                hdr_post_frame_.record_pass(context.recorder(), target, frame_slot);
            });
        return {
            .graph = graph.compile(),
            .scene_color = scene_color,
            .post_scene_color = post_scene_color,
            .scene_depth = depth,
            .cloud = cloud_frame,
            .clouds_enabled = clouds_enabled,
        };
    }

    void complete_shadow_frame_recording() {
        shadow_updated_last_frame_ = shadow_update_this_frame_;
        if (shadow_update_this_frame_) {
            update_terrain_shadow_cache(shadow_cache_, product_.diagnostics.content_hash,
                                        frame_shadow_projection_);
        }
        shadow_depth_is_sampled_ = true;
    }

    void record_target(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                       cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
                       cubey::render::RenderGraphTextureState target_initial_state,
                       cubey::render::RenderGraphTextureState target_final_state,
                       cubey::render::RenderGraphCommandBufferMode command_buffer_mode) {
        frame_camera_transform_ = current_camera_transform();
        const cubey::render::AtmosphereEnvironmentFrameUniforms atmosphere_frame =
            atmosphere_uniforms(target.extent);
        const cubey::render::AtmosphereEnvironmentLighting lighting =
            cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment);
        const TerrainShadowProjection candidate_shadow = terrain_shadow_projection(
            {
                .outer_radius_m = product_.request.outer_radius_m,
                .minimum_height_m = product_.diagnostics.minimum_height_m,
                .maximum_height_m = product_.diagnostics.maximum_height_m,
            },
            lighting.primary_light_direction);
        frame_primary_light_above_horizon_ = candidate_shadow.light_above_horizon;
        shadow_update_this_frame_ = terrain_shadow_update_required(
            shadow_cache_, runtime_config_.shadows, product_.diagnostics.content_hash,
            lighting.primary_light_direction);
        frame_shadow_projection_ = shadow_update_this_frame_ || !shadow_cache_.valid
                                       ? candidate_shadow
                                       : shadow_cache_.projection;
        const bool sample_shadows = runtime_config_.shadows &&
                                    frame_primary_light_above_horizon_ &&
                                    (shadow_cache_.valid || shadow_update_this_frame_);
        environment_material().upload(
            frame_slot, terrain_environment_gpu_parameters(atmosphere_frame, lighting,
                                                           frame_shadow_projection_,
                                                           sample_shadows));
        atmosphere_background_.upload(frame_slot, atmosphere_frame);
        std::optional<cubey::CloudEnvironmentRuntimeFrame> cloud_frame;
        if (cloud_layer_enabled()) {
            cloud_frame = current_cloud_frame(target.extent, lighting);
        }
        hdr_post_frame_.upload(frame_slot,
                               cubey::render::hdr_post_uniforms(target.format, display_exposure()));

        const CompiledTerrainGraph render_graph = current_render_graph(
            target, frame_slot, target_initial_state, target_final_state, cloud_frame);
        const auto prepare_resources = [this, &device, frame_slot, &render_graph](
                                           const cubey::render::RenderGraphResourceSet& resources) {
            hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, render_graph.graph,
                                                          resources, render_graph.post_scene_color);
            if (render_graph.clouds_enabled) {
                cloud_runtime_.update_surface_descriptors(
                    device, frame_slot, render_graph.graph, resources, render_graph.cloud,
                    render_graph.scene_color, render_graph.scene_depth);
            }
        };
        cubey::vulkan::GpuTimestampProfiler* profiler = gpu_profiler();
        if (profiler != nullptr &&
            command_buffer_mode == cubey::render::RenderGraphCommandBufferMode::BeginAndEnd) {
            const cubey::vulkan::CommandRecorder recorder(command_buffer);
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            profiler->begin_frame(command_buffer, frame_slot.index);
            graph_executor_.record(
                cubey::render::RenderGraphFrameRecordInfo{
                    .device = &device,
                    .command_buffer = command_buffer,
                    .frame_slot = frame_slot,
                    .label = "vkEndCommandBuffer terrain",
                    .command_buffer_mode =
                        cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                    .profiler = profiler,
                },
                render_graph.graph, prepare_resources);
            complete_shadow_frame_recording();
            if (render_graph.clouds_enabled) {
                cloud_runtime_.complete_surface_frame(frame_slot, render_graph.cloud);
            }
            recorder.end("vkEndCommandBuffer terrain");
            return;
        }
        if (profiler != nullptr) {
            profiler->begin_frame(command_buffer, frame_slot.index);
        }
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer terrain",
                .command_buffer_mode = command_buffer_mode,
                .profiler = profiler,
            },
            render_graph.graph, prepare_resources);
        complete_shadow_frame_recording();
        if (render_graph.clouds_enabled) {
            cloud_runtime_.complete_surface_frame(frame_slot, render_graph.cloud);
        }
    }

    [[nodiscard]] const cubey::render::Mesh& center_mesh() const {
        if (!center_mesh_.has_value()) {
            throw std::runtime_error("terrain center mesh is not initialized");
        }
        return center_mesh_.value();
    }

    [[nodiscard]] const cubey::render::Mesh& stage_proxy_mesh() const {
        if (!stage_proxy_mesh_.has_value()) {
            throw std::runtime_error("terrain stage proxy mesh is not initialized");
        }
        return stage_proxy_mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& terrain_forward_pass() const {
        if (!terrain_pass_.has_value()) {
            throw std::runtime_error("terrain forward pass is not initialized");
        }
        return terrain_pass_.value();
    }

    [[nodiscard]] const cubey::render::ShadowMapPass3D& shadow_pass() const {
        if (!shadow_pass_.has_value()) {
            throw std::runtime_error("terrain shadow pass is not initialized");
        }
        return shadow_pass_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& stage_proxy_pipeline() const {
        if (!stage_proxy_pipeline_.has_value()) {
            throw std::runtime_error("terrain stage proxy pipeline is not initialized");
        }
        return stage_proxy_pipeline_.value();
    }

    [[nodiscard]] const cubey::render::MaterialInstance& detail_material() const {
        if (!detail_material_.has_value()) {
            throw std::runtime_error("terrain detail material is not initialized");
        }
        return detail_material_.value();
    }

    [[nodiscard]]
    const cubey::render::FrameUniformMaterialInstance<TerrainEnvironmentGpuParameters>&
    environment_material() const {
        if (!environment_material_.has_value()) {
            throw std::runtime_error("terrain environment material is not initialized");
        }
        return environment_material_.value();
    }

    RunConfig run_config_;
    TerrainRuntimeConfig runtime_config_{};
    TerrainRasterHeightSource source_;
    TerrainBackdropPlacementPlan placement_stage_{};
    TerrainBackdropProduct product_{};
    TerrainPlacementMode edit_placement_mode_ = TerrainPlacementMode::Selected;
    std::uint32_t edit_placement_index_ = 0U;
    std::future<TerrainPlacementBuild> placement_build_future_{};
    std::string placement_rebuild_error_{};
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_;
    cubey::Transform3D frame_camera_transform_{};
    cubey::AtmosphereEnvironmentRunState atmosphere_state_{};
    cubey::CloudEnvironmentConfig clouds_config_{};
    cubey::CloudEnvironmentRuntime cloud_runtime_{};
    float baked_foreground_height_m_ = 500.0F;
    float foreground_height_m_ = kTerrainDefaultForegroundHeightM;

    std::optional<cubey::render::Mesh> center_mesh_{};
    std::vector<cubey::render::Mesh> sector_meshes_{};
    mutable TerrainBackdropDrawPlan latest_draw_plan_{};
    std::optional<cubey::render::Mesh> stage_proxy_mesh_{};
    std::optional<cubey::render::ShadowMapPass3D> shadow_pass_{};
    TerrainShadowCacheState shadow_cache_{};
    TerrainShadowProjection frame_shadow_projection_{};
    bool shadow_update_this_frame_ = false;
    bool shadow_updated_last_frame_ = false;
    bool shadow_depth_is_sampled_ = false;
    bool frame_primary_light_above_horizon_ = false;
    std::optional<TerrainBackdropMaterialTexture> material_texture_{};
    std::optional<cubey::render::MaterialInstance> detail_material_{};
    std::optional<cubey::render::FrameUniformMaterialInstance<TerrainEnvironmentGpuParameters>>
        environment_material_{};
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_atlases_{};
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_{};
    cubey::render::HdrPostFrame hdr_post_frame_{};
    std::optional<cubey::render::ForwardScenePass3D> terrain_pass_{};
    std::optional<cubey::render::GraphicsPipelineResource> stage_proxy_pipeline_{};
    cubey::render::RenderGraphFrameExecutor graph_executor_{};
    std::optional<cubey::vulkan::GpuTimestampProfiler> gpu_profiler_{};
    bool global_resources_created_ = false;
};

} // namespace

int run_terrain(const cubey::RunConfig& config) {
    TerrainApp app(config);
    return app.run();
}

} // namespace cubey::projects::terrain
