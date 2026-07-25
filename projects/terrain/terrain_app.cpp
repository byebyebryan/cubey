#include "terrain_app.h"

#include "terrain_config.h"
#include "terrain_product_adapter.h"
#include "terrain_source_catalog.h"

#include <cubey/asset/terrain_raster_height_source.h>
#include <cubey/core/jobs.h>
#include <cubey/engine/atmosphere_background_atlas_runtime.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/cloud_environment_runtime.h>
#include <cubey/engine/staged_resource.h>
#include <cubey/engine/terrain_backdrop_runtime.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/cloud_environment_ui.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/imgui_helpers.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/procedural/artifact_cache.h>
#include <cubey/procedural/hash.h>
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
#include <cubey/terrain/terrain_backdrop_placement.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_timestamps.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
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

#ifndef CUBEY_TERRAIN_DEFAULT_SURFACE_FIELDS
#error "CUBEY_TERRAIN_DEFAULT_SURFACE_FIELDS must be defined by the terrain CMake target"
#endif

#ifndef CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSETS
#error "CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSETS must be defined by the terrain CMake target"
#endif

#ifndef CUBEY_TERRAIN_SOURCE_PRESET_CATALOG
#error "CUBEY_TERRAIN_SOURCE_PRESET_CATALOG must be defined by the terrain CMake target"
#endif

#ifndef CUBEY_TERRAIN_OPTIONAL_PRESET_ASSETS
#error "CUBEY_TERRAIN_OPTIONAL_PRESET_ASSETS must be defined by the terrain CMake target"
#endif

namespace cubey::projects::terrain {
namespace {

using Clock = std::chrono::steady_clock;
using cubey::asset::TerrainRasterProvenance;
using cubey::terrain::plan_terrain_backdrop_placement;
using cubey::terrain::TerrainBackdropPlacementPlan;
using cubey::terrain::TerrainBackdropPlacementRequest;
using cubey::terrain::TerrainBackdropProduct;
using cubey::terrain::TerrainBackdropProductInfo;
using cubey::terrain::TerrainBackdropStagePlan;
using cubey::terrain::TerrainDirectionalPlacementPlan;

constexpr VkFormat kTerrainSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr float kTerrainHeadlessOrbitSpeed = 0.18F;
constexpr std::uint32_t kTerrainGpuProfilerPassCapacity = 16U;
constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
constexpr float kTerrainMinimumForegroundHeightM = 2.0F;
constexpr float kTerrainDefaultForegroundHeightM = 200.0F;
constexpr float kTerrainMaximumForegroundHeightM = 1'000.0F;
constexpr float kTerrainDefaultTimeHours = 9.0F;
constexpr float kTerrainCloudSceneDepthFadeM = 500.0F;
constexpr float kTerrainInspectionPitchLimitRadians = 1'000.0F;
constexpr float kTerrainInspectionMaximumOrbitRadiusM = 1'000.0F;

struct TerrainSourcePaths {
    std::filesystem::path heightfield{};
    std::filesystem::path surface_fields{};
};

struct TerrainSourceChoice {
    std::string id{};
    std::string label{};
    TerrainSourcePaths paths{};
    std::optional<std::uint64_t> expected_seed{};
    bool climate_required = false;
    std::optional<TerrainSourceCatalogEntry> preset{};
};

[[nodiscard]] bool same_source_path(const std::filesystem::path& lhs,
                                    const std::filesystem::path& rhs) {
    return std::filesystem::absolute(lhs).lexically_normal() ==
           std::filesystem::absolute(rhs).lexically_normal();
}

[[nodiscard]] std::vector<TerrainSourceChoice>
terrain_source_choices(const TerrainRuntimeConfig& startup_config,
                       std::string& preset_catalog_error) {
    std::vector<TerrainSourceChoice> result{
        TerrainSourceChoice{
            .id = "startup",
            .label = "Startup source",
            .paths =
                {
                    .heightfield = startup_config.heightfield_path,
                    .surface_fields = startup_config.surface_fields_path,
                },
            .expected_seed = startup_config.expected_seed,
        },
    };

    std::optional<TerrainSourcePresetCatalog> preset_catalog;
    try {
        preset_catalog = load_terrain_source_preset_catalog(
            std::filesystem::path(CUBEY_TERRAIN_SOURCE_PRESET_CATALOG),
            std::filesystem::path(CUBEY_TERRAIN_OPTIONAL_PRESET_ASSETS));
        const TerrainSourcePaths default_paths{
            .heightfield = std::filesystem::path(CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD),
            .surface_fields = std::filesystem::path(CUBEY_TERRAIN_DEFAULT_SURFACE_FIELDS),
        };
        if (same_source_path(startup_config.heightfield_path, default_paths.heightfield)) {
            result.front().id = "preset/" + preset_catalog->default_id;
            result.front().label = "Preset: " + preset_catalog->default_label;
            result.front().expected_seed = preset_catalog->default_seed;
        } else {
            result.push_back({
                .id = "preset/" + preset_catalog->default_id,
                .label = "Preset: " + preset_catalog->default_label,
                .paths = default_paths,
                .expected_seed = preset_catalog->default_seed,
            });
        }
    } catch (const std::exception& catalog_error) {
        preset_catalog_error = catalog_error.what();
    }

    constexpr std::array calibration_sources{
        std::pair{"hot-dry", "Calibration: Hot / dry"},
        std::pair{"hot-wet", "Calibration: Hot / wet"},
        std::pair{"cool-wet", "Calibration: Cool / wet"},
        std::pair{"cold-dry", "Calibration: Cold / dry"},
        std::pair{"cold-wet", "Calibration: Cold / wet"},
    };
    const std::filesystem::path calibration_root(CUBEY_TERRAIN_CLIMATE_CALIBRATION_ASSETS);
    for (const auto& [id, label] : calibration_sources) {
        const std::filesystem::path directory = calibration_root / id;
        result.push_back({
            .id = "calibration/" + std::string(id),
            .label = label,
            .paths = {.heightfield = directory, .surface_fields = directory},
            .expected_seed = 0U,
            .climate_required = true,
        });
    }

    if (preset_catalog.has_value()) {
        for (TerrainSourceCatalogEntry entry : preset_catalog->optional_presets) {
            TerrainSourceChoice choice{
                .id = "preset/" + entry.id,
                .label = "Preset: " + entry.label,
                .paths =
                    {
                        .heightfield = entry.heightfield_path,
                        .surface_fields = entry.surface_fields_path,
                    },
                .expected_seed = entry.seed,
                .climate_required = true,
                .preset = std::move(entry),
            };
            if (same_source_path(startup_config.heightfield_path, choice.paths.heightfield)) {
                result.front() = std::move(choice);
            } else {
                result.push_back(std::move(choice));
            }
        }
    }
    return result;
}

[[nodiscard]] bool terrain_manifest_available(const std::filesystem::path& path,
                                              std::string_view manifest) {
    std::error_code error;
    const std::filesystem::path candidate =
        std::filesystem::is_directory(path, error) ? path / manifest : path;
    return !error && std::filesystem::is_regular_file(candidate, error) && !error;
}

[[nodiscard]] bool terrain_source_climate_available(const TerrainSourcePaths& paths) {
    return terrain_manifest_available(paths.surface_fields, "surface-fields.json");
}

[[nodiscard]] TerrainSourceAvailability
terrain_source_choice_availability(const TerrainSourceChoice& choice) {
    if (choice.preset.has_value()) {
        return terrain_source_availability(*choice.preset);
    }

    const bool heightfield =
        terrain_manifest_available(choice.paths.heightfield, "heightfield.json");
    const bool surface_fields =
        !choice.climate_required || terrain_source_climate_available(choice.paths);
    if (heightfield && surface_fields) {
        return TerrainSourceAvailability::Available;
    }

    std::error_code heightfield_error;
    const bool heightfield_storage =
        std::filesystem::exists(choice.paths.heightfield, heightfield_error) && !heightfield_error;
    std::error_code surface_fields_error;
    const bool surface_fields_storage =
        choice.climate_required &&
        std::filesystem::exists(choice.paths.surface_fields, surface_fields_error) &&
        !surface_fields_error;
    return !heightfield_storage && !surface_fields_storage ? TerrainSourceAvailability::NotGenerated
                                                           : TerrainSourceAvailability::Incomplete;
}

[[nodiscard]] std::string terrain_source_choice_label(const TerrainSourceChoice& choice,
                                                      TerrainSourceAvailability availability) {
    if (availability == TerrainSourceAvailability::Available) {
        return choice.label;
    }
    return choice.label + " (" + std::string(terrain_source_availability_name(availability)) + ")";
}

enum class TerrainProductLoadPhase : std::uint8_t {
    None,
    Waiting,
    LoadingHeightfield,
    LoadingClimate,
    PlanningPlacement,
    PreparingProduct,
    UploadingProduct,
};

[[nodiscard]] constexpr std::string_view
terrain_product_load_phase_name(TerrainProductLoadPhase phase) noexcept {
    switch (phase) {
    case TerrainProductLoadPhase::None:
        return "idle";
    case TerrainProductLoadPhase::Waiting:
        return "waiting for worker";
    case TerrainProductLoadPhase::LoadingHeightfield:
        return "loading heightfield";
    case TerrainProductLoadPhase::LoadingClimate:
        return "loading climate fields";
    case TerrainProductLoadPhase::PlanningPlacement:
        return "planning backdrop placement";
    case TerrainProductLoadPhase::PreparingProduct:
        return "loading or building render product";
    case TerrainProductLoadPhase::UploadingProduct:
        return "uploading render product";
    }
    return "unknown";
}

constexpr std::array<std::string_view, 8U> kTerrainGpuTimingLabels{
    "terrain shadow", "terrain atmosphere", "terrain surface", "terrain stage proxy",
    "cloud march",    "cloud temporal",     "cloud composite", "terrain post",
};

struct TerrainStageProxyPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 camera_position{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 object_translation{0.0F, 0.0F, 0.0F, 0.0F};
};

struct TerrainProductBuild {
    TerrainRuntimeConfig config{};
    std::size_t source_choice_index = 0U;
    std::optional<TerrainRasterHeightSource> replacement_source{};
    std::optional<TerrainRasterClimateSource> replacement_climate_source{};
    TerrainBackdropPlacementPlan placement{};
    TerrainBackdropClimateDiagnostics climate_diagnostics{};
    TerrainProductCacheDiagnostics cache_diagnostics{};
    double source_load_milliseconds = 0.0;
    double climate_load_milliseconds = 0.0;
    double placement_milliseconds = 0.0;
    TerrainBackdropProduct product{};
};

struct TerrainResidentBuild {
    TerrainProductBuild prepared{};
    TerrainBackdropProductInfo product_info{};
    cubey::TerrainBackdropResidentProduct resident{};
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

[[nodiscard]] std::uint64_t profile_frame_index(std::uint64_t frame_index) {
    return frame_index == 0U ? 0U : frame_index - 1U;
}

[[nodiscard]] double elapsed_milliseconds(Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

[[nodiscard]] constexpr std::string_view
terrain_product_preparation_source_name(TerrainProductPreparationSource source) noexcept {
    switch (source) {
    case TerrainProductPreparationSource::Cache:
        return "cache";
    case TerrainProductPreparationSource::Generated:
        return "generated";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view
terrain_cache_lookup_name(cubey::procedural::ProceduralArtifactCacheLoadOutcome outcome) noexcept {
    switch (outcome) {
    case cubey::procedural::ProceduralArtifactCacheLoadOutcome::Hit:
        return "hit";
    case cubey::procedural::ProceduralArtifactCacheLoadOutcome::Miss:
        return "miss";
    case cubey::procedural::ProceduralArtifactCacheLoadOutcome::Rejected:
        return "rejected";
    }
    return "unknown";
}

[[nodiscard]] std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                                          cubey::render::FrameSlot frame_slot) {
    if (frame_index > frame_slot.count) {
        return frame_index - static_cast<std::uint64_t>(frame_slot.count) - 1U;
    }
    return profile_frame_index(frame_index);
}

[[nodiscard]] std::uint32_t sha256_word(std::string_view hash, std::size_t offset) noexcept {
    if (offset + 8U > hash.size()) {
        return 0U;
    }
    std::uint32_t value = 0U;
    for (std::size_t index = offset; index < offset + 8U; ++index) {
        const char digit = hash[index];
        const std::uint32_t nibble = digit >= '0' && digit <= '9'
                                         ? static_cast<std::uint32_t>(digit - '0')
                                         : static_cast<std::uint32_t>(digit - 'a' + 10);
        value = (value << 4U) | nibble;
    }
    return value;
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

[[nodiscard]] std::optional<TerrainRasterClimateSource>
load_climate_source(const TerrainRuntimeConfig& config, const TerrainRasterHeightSource& source) {
    if (config.surface_fields_path.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    if (!std::filesystem::exists(config.surface_fields_path, error) || error) {
        if (config.surface_model == TerrainSurfaceModel::ClimateTransition) {
            throw std::runtime_error(
                "terrain surface fields are missing: " + config.surface_fields_path.string() +
                "\nGenerate the canonical companion with:\n  cmake --build --preset dev --target "
                "cubey_terrain_generate_surface_study_asset");
        }
        return std::nullopt;
    }
    TerrainRasterClimateSource climate(config.surface_fields_path);
    validate_terrain_climate_binding(source, climate);
    return climate;
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

[[nodiscard]] std::uint64_t
terrain_product_placement_parameter_hash(const TerrainRuntimeConfig& config,
                                         const TerrainBackdropPlacementPlan& placement) {
    cubey::procedural::ProceduralHashBuilder hash;
    hash.append_string("terrain-product-placement-v1");
    hash.append_u32(static_cast<std::uint32_t>(config.placement));
    hash.append_u32(config.placement_index);
    hash.append_float32(placement.placement.source_focus_xz.x);
    hash.append_float32(placement.placement.source_focus_xz.y);
    return hash.value();
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

[[nodiscard]] TerrainBackdropPlacementPlan loading_placement_stage() {
    TerrainBackdropPlacementPlan placement;
    placement.stage.source_center_height_m = 0.0F;
    placement.stage.target_height_m = 500.0F;
    placement.stage.minimum_camera_clearance_m = kTerrainDefaultForegroundHeightM;
    placement.stage.orbit_min_radius_m = 25.0F;
    placement.stage.orbit_default_radius_m = 350.0F;
    placement.stage.orbit_max_radius_m = kTerrainInspectionMaximumOrbitRadiusM;
    placement.stage.orbit_default_elevation_radians = 0.12F;
    return placement;
}

class TerrainApp {
  public:
    explicit TerrainApp(RunConfig config)
        : run_config_(std::move(config)),
          runtime_config_(terrain_runtime_config_from_run_config(
              run_config_, std::filesystem::path(CUBEY_TERRAIN_DEFAULT_HEIGHTFIELD),
              std::filesystem::path(CUBEY_TERRAIN_DEFAULT_SURFACE_FIELDS))),
          startup_runtime_config_(runtime_config_),
          product_cache_({.root = cubey::procedural::default_procedural_artifact_cache_root()}),
          product_builds_(product_jobs_), placement_stage_(loading_placement_stage()),
          orbit_controller_(cubey::OrbitControllerConfig{
              .min_pitch = -kTerrainInspectionPitchLimitRadians,
              .max_pitch = kTerrainInspectionPitchLimitRadians,
          }),
          camera_(cubey::Camera3DConfig{
              .fovy_radians = 40.0F * kDegreesToRadians,
              .near_z = 0.1F,
              .far_z = 100'000.0F,
          }),
          atmosphere_state_(terrain_atmosphere_state(run_config_)),
          clouds_config_(terrain_cloud_config(run_config_, atmosphere_state_.environment)) {
        source_choices_ = terrain_source_choices(startup_runtime_config_, preset_catalog_error_);
        source_choice_availability_.resize(source_choices_.size());
        source_choice_climate_available_.resize(source_choices_.size());
        refresh_source_choice_availability();
        edit_placement_mode_ = runtime_config_.placement;
        edit_placement_index_ = runtime_config_.placement_index;
        edit_surface_model_ = runtime_config_.surface_model;
        edit_source_choice_index_ = source_choice_index_;
        configure_camera_for_placement(true);
        request_product_build(runtime_config_, source_choice_index_);
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
            const cubey::vulkan::GpuSubmissionTicket retire_after =
                context.frame_resources().latest_submitted_ticket();
            poll_product_build(context.device(), context.gpu(), retire_after);
            poll_atmosphere_atlases(context.device(), context.gpu(), retire_after);
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
                .triangles = terrain_runtime_.draw_plan().submitted_triangle_count,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            destroy_all_resources(context.gpu());
        };

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
            finish_atmosphere_atlases(context.device(), context.gpu());
            product_builds_.finish(context.gpu());
            install_ready_product_build(context.device(), context.gpu(), {});
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
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext& context) {
            destroy_all_resources(context.gpu());
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void refresh_source_choice_availability() {
        for (std::size_t index = 0U; index < source_choices_.size(); ++index) {
            const TerrainSourceChoice& choice = source_choices_[index];
            const TerrainSourceAvailability availability =
                terrain_source_choice_availability(choice);
            source_choice_availability_[index] = availability;
            source_choice_climate_available_[index] =
                availability == TerrainSourceAvailability::Available &&
                terrain_source_climate_available(choice.paths);
        }
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        if (!cubey::host::begin_control_panel("Terrain", {.width = 410.0F})) {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Source");
        refresh_source_choice_availability();
        const cubey::StagedResourceStatus& build_status = product_builds_.status();
        const bool product_build_pending = product_builds_.busy();
        const std::string source_preview =
            terrain_source_choice_label(source_choices_.at(edit_source_choice_index_),
                                        source_choice_availability_.at(edit_source_choice_index_));
        ImGui::BeginDisabled(product_build_pending);
        if (ImGui::BeginCombo("Terrain source", source_preview.c_str())) {
            for (std::size_t index = 0U; index < source_choices_.size(); ++index) {
                const TerrainSourceChoice& choice = source_choices_[index];
                const TerrainSourceAvailability availability = source_choice_availability_[index];
                const bool available = availability == TerrainSourceAvailability::Available;
                const bool selected = index == edit_source_choice_index_;
                const std::string label = terrain_source_choice_label(choice, availability);
                ImGui::BeginDisabled(!available);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    edit_source_choice_index_ = index;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (source_.has_value()) {
            ImGui::Text("Showing: %s", source_choices_.at(source_choice_index_).label.c_str());
        } else {
            ImGui::TextDisabled("Showing: loading placeholder");
        }
        if (product_build_pending) {
            const TerrainProductLoadPhase load_phase =
                product_load_phase_.load(std::memory_order_relaxed);
            const std::string_view phase = terrain_product_load_phase_name(load_phase);
            ImGui::Text("Loading: %s", build_status.generation.label.c_str());
            if (product_load_started_at_.has_value()) {
                ImGui::Text("Phase: %.*s (%.1f s)", static_cast<int>(phase.size()), phase.data(),
                            elapsed_milliseconds(*product_load_started_at_) * 0.001);
            } else {
                ImGui::Text("Phase: %.*s", static_cast<int>(phase.size()), phase.data());
            }
            if (source_.has_value()) {
                ImGui::TextDisabled("Showing current terrain until replacement is ready");
            }
        }
        if (source_.has_value()) {
            const TerrainRasterProvenance& provenance = source_->provenance();
            const TerrainHeightSourceMetadata metadata = source_->metadata();
            ImGui::Text("%.*s, seed %llu", static_cast<int>(metadata.id.size()), metadata.id.data(),
                        static_cast<unsigned long long>(metadata.seed));
            ImGui::Text("%u x %u at %.0f m", source_->width(), source_->height(),
                        source_->sample_spacing_m());
            const float width_km =
                static_cast<float>(source_->width() - 1U) * source_->sample_spacing_m() * 0.001F;
            const float height_km =
                static_cast<float>(source_->height() - 1U) * source_->sample_spacing_m() * 0.001F;
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
        } else {
            ImGui::TextDisabled("No resident terrain source");
        }
        if (!preset_catalog_error_.empty()) {
            ImGui::TextWrapped("Preset catalog: %s", preset_catalog_error_.c_str());
        }

        ImGui::SeparatorText("Placement");
        ImGui::BeginDisabled(product_build_pending);
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

        ImGui::SeparatorText("Surface study");
        if (ImGui::RadioButton("Mineral control",
                               edit_surface_model_ == TerrainSurfaceModel::MineralControl)) {
            edit_surface_model_ = TerrainSurfaceModel::MineralControl;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Landform",
                               edit_surface_model_ == TerrainSurfaceModel::LandformTransition)) {
            edit_surface_model_ = TerrainSurfaceModel::LandformTransition;
        }
        ImGui::BeginDisabled(!source_choice_climate_available_[edit_source_choice_index_]);
        if (ImGui::RadioButton("Climate",
                               edit_surface_model_ == TerrainSurfaceModel::ClimateTransition)) {
            edit_surface_model_ = TerrainSurfaceModel::ClimateTransition;
        }
        ImGui::EndDisabled();
        const std::string_view active_surface =
            terrain_surface_model_name(runtime_config_.surface_model);
        ImGui::Text("Active surface: %.*s", static_cast<int>(active_surface.size()),
                    active_surface.data());
        if (climate_source_.has_value()) {
            const TerrainRasterClimateMetadata& climate = climate_source_->metadata();
            ImGui::Text("Climate grid: %u x %u at %.0f m", climate.width, climate.height,
                        climate.sample_spacing_m);
            ImGui::Text("Climate hash: %.*s", 12, climate.climate_sha256.c_str());
        } else {
            ImGui::TextDisabled("Climate companion unavailable");
        }

        const bool product_edit_dirty = edit_source_choice_index_ != source_choice_index_ ||
                                        edit_placement_mode_ != placement_stage_.mode ||
                                        edit_placement_index_ != placement_stage_.sample_index ||
                                        edit_surface_model_ != runtime_config_.surface_model;
        const bool edited_source_available =
            source_choice_availability_[edit_source_choice_index_] ==
            TerrainSourceAvailability::Available;
        ImGui::BeginDisabled(!source_.has_value() || !product_edit_dirty || product_build_pending ||
                             !edited_source_available);
        if (ImGui::Button("Apply terrain")) {
            start_product_build();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!source_.has_value() || !product_edit_dirty || product_build_pending);
        if (ImGui::Button("Revert")) {
            edit_source_choice_index_ = source_choice_index_;
            edit_placement_mode_ = placement_stage_.mode;
            edit_placement_index_ = placement_stage_.sample_index;
            edit_surface_model_ = runtime_config_.surface_model;
        }
        ImGui::EndDisabled();

        if (build_status.prepare_milliseconds > 0.0 || build_status.install_milliseconds > 0.0) {
            ImGui::Text("Prepare / install: %.1f / %.1f ms", build_status.prepare_milliseconds,
                        build_status.install_milliseconds);
        }
        if (!build_status.error.empty()) {
            ImGui::TextWrapped("Terrain error: %s", build_status.error.c_str());
        }
        if (!product_rebuild_error_.empty()) {
            ImGui::TextWrapped("Terrain activation error: %s", product_rebuild_error_.c_str());
        }
        if (source_.has_value()) {
            const std::string_view preparation_source =
                terrain_product_preparation_source_name(product_cache_diagnostics_.source);
            const std::string_view lookup =
                terrain_cache_lookup_name(product_cache_diagnostics_.lookup);
            ImGui::Text("Source / climate / placement: %.1f / %.1f / %.1f ms",
                        source_load_milliseconds_, climate_load_milliseconds_,
                        placement_milliseconds_);
            ImGui::Text("Product preparation: %.*s, lookup %.*s",
                        static_cast<int>(preparation_source.size()), preparation_source.data(),
                        static_cast<int>(lookup.size()), lookup.data());
            ImGui::Text("Cache load / decode: %.1f / %.1f ms",
                        product_cache_diagnostics_.load_milliseconds,
                        product_cache_diagnostics_.decode_milliseconds);
            ImGui::Text("Generate / encode / store: %.1f / %.1f / %.1f ms",
                        product_cache_diagnostics_.generation_milliseconds,
                        product_cache_diagnostics_.encode_milliseconds,
                        product_cache_diagnostics_.store_milliseconds);
            if (!product_cache_diagnostics_.diagnostic.empty()) {
                ImGui::TextWrapped("Cache diagnostic: %s",
                                   product_cache_diagnostics_.diagnostic.c_str());
            }
        }

        const TerrainDirectionalPlacementPlan& placement = placement_stage_.placement;
        const std::string_view placement_name = terrain_placement_mode_name(placement_stage_.mode);
        ImGui::Text("Active: %.*s", static_cast<int>(placement_name.size()), placement_name.data());
        if (placement_stage_.mode == TerrainPlacementMode::RawSample) {
            ImGui::Text("Active sample: %u", placement_stage_.sample_index);
        }
        ImGui::Text("Focus: %.3f, %.3f km", placement.source_focus_xz.x * 0.001F,
                    placement.source_focus_xz.y * 0.001F);
        ImGui::Text("Local stage: %s", placement.local_contract_satisfied ? "pass" : "fail");
        ImGui::Text("Mountain composition: %s",
                    placement.contract_satisfied ? "preferred" : "best available");
        ImGui::Text("Score: %.3f", placement.score);
        ImGui::Text("Local relief (%.0f m): %.1f / %.1f m", placement.local_radius_m,
                    placement.local_relief_m, placement.maximum_local_relief_m);
        ImGui::Text("P95 slope: %.3f / %.3f", placement.local_p95_slope,
                    placement.maximum_local_p95_slope);
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
        ImGui::SliderFloat("Aerial perspective", &runtime_config_.aerial_perspective_strength, 0.0F,
                           1.0F, "%.2f");

        constexpr std::array diagnostics{
            TerrainDebugView::Surface,
            TerrainDebugView::Height,
            TerrainDebugView::Slope,
            TerrainDebugView::Clay,
            TerrainDebugView::Normal,
            TerrainDebugView::ClassificationNormal,
            TerrainDebugView::MaterialWeights,
            TerrainDebugView::Vegetation,
            TerrainDebugView::Moisture,
            TerrainDebugView::AmbientVisibility,
            TerrainDebugView::AmbientLighting,
            TerrainDebugView::DirectLighting,
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
        ImGui::Text("Render stride: %u", product_info_.request.render_stride);
        ImGui::Text(
            "Product / center triangles: %llu / %llu",
            static_cast<unsigned long long>(product_info_.diagnostics.render_triangle_count),
            static_cast<unsigned long long>(
                product_info_.diagnostics.center_render_triangle_count));
        ImGui::Text("Submitted sectors: %u", terrain_runtime_.draw_plan().submitted_sector_count);
        ImGui::Text("Submitted triangles: %u",
                    terrain_runtime_.draw_plan().submitted_triangle_count);
        ImGui::Text("Cached source samples: %llu",
                    static_cast<unsigned long long>(product_info_.diagnostics.source_sample_count));
        ImGui::Text("Sampled / render vertices: %llu / %llu",
                    static_cast<unsigned long long>(product_info_.diagnostics.sampled_vertex_count),
                    static_cast<unsigned long long>(product_info_.diagnostics.render_vertex_count));
        const double retained_vertex_ratio =
            product_info_.diagnostics.sampled_vertex_count == 0U
                ? 0.0
                : static_cast<double>(product_info_.diagnostics.render_vertex_count) /
                      static_cast<double>(product_info_.diagnostics.sampled_vertex_count);
        ImGui::Text("Vertex compaction: %.1f%% removed", (1.0 - retained_vertex_ratio) * 100.0);
        ImGui::Text("Mesh upload: %.2f MiB / %u transfer%s",
                    static_cast<double>(terrain_runtime_.mesh_upload_bytes()) / (1024.0 * 1024.0),
                    terrain_runtime_.mesh_upload_transfer_submissions(),
                    terrain_runtime_.mesh_upload_transfer_submissions() == 1U ? "" : "s");
        ImGui::Text("Mean vegetation / moisture: %.3f / %.3f",
                    product_info_.diagnostics.mean_vegetation,
                    product_info_.diagnostics.mean_moisture);
        ImGui::Text("Shadow map: %u x %u, %s", cubey::render::kTerrainShadowMapExtent,
                    cubey::render::kTerrainShadowMapExtent,
                    terrain_runtime_.shadow_cache().valid ? "valid" : "pending");
        ImGui::Text("Shadow texel / depth span: %.1f m / %.0f m",
                    terrain_runtime_.shadow_projection().texel_world_size_m,
                    terrain_runtime_.shadow_projection().depth_span_m);
        ImGui::Text("Shadow casters: %llu triangles (outer backdrop)",
                    static_cast<unsigned long long>(shadow_caster_triangle_count()));
        ImGui::Text("Shadow updates: %llu",
                    static_cast<unsigned long long>(terrain_runtime_.shadow_cache().update_count));
        if (!terrain_runtime_.shadow_projection().light_above_horizon) {
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

    void start_product_build() {
        if (product_builds_.busy()) {
            return;
        }

        TerrainRuntimeConfig config = runtime_config_;
        const std::size_t requested_source_choice_index = edit_source_choice_index_;
        if (requested_source_choice_index != source_choice_index_) {
            const TerrainSourceChoice& choice = source_choices_.at(requested_source_choice_index);
            config.heightfield_path = choice.paths.heightfield;
            config.surface_fields_path = choice.paths.surface_fields;
            config.expected_seed = choice.expected_seed;
        }
        config.placement = edit_placement_mode_;
        config.placement_index = edit_placement_index_;
        config.surface_model = edit_surface_model_;
        product_rebuild_error_.clear();
        try {
            request_product_build(std::move(config), requested_source_choice_index);
        } catch (const std::exception& error) {
            product_rebuild_error_ = error.what();
        } catch (...) {
            product_rebuild_error_ = "unable to start terrain product rebuild";
        }
    }

    void request_product_build(TerrainRuntimeConfig config,
                               std::size_t requested_source_choice_index) {
        const std::string label = source_choices_.at(requested_source_choice_index).label;
        product_load_started_at_ = Clock::now();
        product_load_phase_.store(TerrainProductLoadPhase::Waiting, std::memory_order_relaxed);
        try {
            static_cast<void>(product_builds_.request(
                label,
                [this, config = std::move(config), requested_source_choice_index]() mutable {
                    TerrainProductBuild build;
                    build.config = config;
                    build.source_choice_index = requested_source_choice_index;
                    product_load_phase_.store(TerrainProductLoadPhase::LoadingHeightfield,
                                              std::memory_order_relaxed);
                    const Clock::time_point source_load_started = Clock::now();
                    build.replacement_source.emplace(require_heightfield(config.heightfield_path));
                    build.source_load_milliseconds = elapsed_milliseconds(source_load_started);
                    product_load_phase_.store(TerrainProductLoadPhase::LoadingClimate,
                                              std::memory_order_relaxed);
                    const Clock::time_point climate_load_started = Clock::now();
                    build.replacement_climate_source =
                        load_climate_source(config, build.replacement_source.value());
                    build.climate_load_milliseconds = elapsed_milliseconds(climate_load_started);
                    product_load_phase_.store(TerrainProductLoadPhase::PlanningPlacement,
                                              std::memory_order_relaxed);
                    const Clock::time_point placement_started = Clock::now();
                    build.placement =
                        make_placement_stage(build.replacement_source.value(), config);
                    build.placement_milliseconds = elapsed_milliseconds(placement_started);
                    const TerrainRasterClimateSource* climate =
                        build.replacement_climate_source ? &build.replacement_climate_source.value()
                                                         : nullptr;
                    product_load_phase_.store(TerrainProductLoadPhase::PreparingProduct,
                                              std::memory_order_relaxed);
                    PreparedProjectTerrainBackdropProduct prepared =
                        prepare_project_terrain_backdrop_product(
                            product_cache_,
                            cubey::terrain::terrain_backdrop_v1_product_request(
                                build.placement.stage, config.render_stride),
                            build.replacement_source.value(), config.surface_model, climate,
                            terrain_product_placement_parameter_hash(config, build.placement));
                    build.product = std::move(prepared.product);
                    build.climate_diagnostics = prepared.climate;
                    build.cache_diagnostics = std::move(prepared.cache);
                    return build;
                },
                [this](cubey::vulkan::GpuOwnerContext& owner, TerrainProductBuild&& prepared) {
                    product_load_phase_.store(TerrainProductLoadPhase::UploadingProduct,
                                              std::memory_order_relaxed);
                    TerrainBackdropProductInfo info =
                        cubey::terrain::terrain_backdrop_product_info(prepared.product);
                    cubey::TerrainBackdropResidentProduct resident =
                        terrain_runtime_.build_resident_product(owner, prepared.product);
                    return TerrainResidentBuild{
                        .prepared = std::move(prepared),
                        .product_info = std::move(info),
                        .resident = std::move(resident),
                    };
                }));
        } catch (...) {
            product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
            product_load_started_at_.reset();
            throw;
        }
    }

    void install_product_build(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                               cubey::vulkan::GpuSubmissionTicket retire_after,
                               TerrainResidentBuild installed) {
        if (!global_resources_created_) {
            throw std::runtime_error("terrain resources are not ready for a product rebuild");
        }

        TerrainProductBuild& build = installed.prepared;
        const bool placement_changed =
            !source_.has_value() || build.source_choice_index != source_choice_index_ ||
            build.config.placement != placement_stage_.mode ||
            build.config.placement_index != placement_stage_.sample_index;
        terrain_runtime_.install_resident_product(gpu, std::move(installed.resident), retire_after);
        source_ = std::move(build.replacement_source);
        climate_source_ = std::move(build.replacement_climate_source);
        source_choice_index_ = build.source_choice_index;
        placement_stage_ = std::move(build.placement);
        climate_diagnostics_ = build.climate_diagnostics;
        product_cache_diagnostics_ = std::move(build.cache_diagnostics);
        source_load_milliseconds_ = build.source_load_milliseconds;
        climate_load_milliseconds_ = build.climate_load_milliseconds;
        placement_milliseconds_ = build.placement_milliseconds;
        product_info_ = std::move(installed.product_info);
        runtime_config_ = std::move(build.config);
        camera_.set_projection(camera_.fovy_radians(), camera_.near_z(),
                               product_info_.request.outer_radius_m * 5.0F);
        if (placement_changed) {
            configure_camera_for_placement(false);
        }
        if (terrain_target_info_.has_value() && !terrain_runtime_.target_resources_created()) {
            terrain_runtime_.create_target_resources(device, terrain_target_info_.value());
        }
        product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
        product_load_started_at_.reset();
    }

    void install_ready_product_build(const cubey::vulkan::Device& device,
                                     cubey::vulkan::GpuRuntime& gpu,
                                     cubey::vulkan::GpuSubmissionTicket retire_after) {
        if (!product_builds_.ready()) {
            return;
        }

        auto result = product_builds_.take_ready();
        install_product_build(device, gpu, retire_after, std::move(result.resident));
        product_rebuild_error_.clear();
    }

    void poll_product_build(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                            cubey::vulkan::GpuSubmissionTicket retire_after) {
        try {
            while (product_builds_.poll(gpu)) {
            }
            install_ready_product_build(device, gpu, retire_after);
            if (!product_builds_.busy() && !product_builds_.ready() &&
                product_builds_.status().phase == cubey::StagedResourcePhase::Failed) {
                product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
                product_load_started_at_.reset();
            }
        } catch (const std::exception& error) {
            product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
            product_load_started_at_.reset();
            product_rebuild_error_ = error.what();
        } catch (...) {
            product_load_phase_.store(TerrainProductLoadPhase::None, std::memory_order_relaxed);
            product_load_started_at_.reset();
            product_rebuild_error_ = "unknown terrain product rebuild error";
        }
    }

    void poll_atmosphere_atlases(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu,
                                 cubey::vulkan::GpuSubmissionTicket retire_after) {
        if (atmosphere_atlases_.poll(gpu, retire_after)) {
            atmosphere_background_.update_texture_bindings(device, atmosphere_atlases_.bindings());
        }
    }

    void finish_atmosphere_atlases(const cubey::vulkan::Device& device,
                                   cubey::vulkan::GpuRuntime& gpu) {
        if (atmosphere_atlases_.finish(gpu)) {
            atmosphere_background_.update_texture_bindings(device, atmosphere_atlases_.bindings());
        }
    }

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (global_resources_created_) {
            return;
        }
        gpu_profiler_.emplace(device, frame_slot_count, kTerrainGpuProfilerPassCapacity);
        const auto stage_proxy_data = terrain_stage_proxy_mesh_data();
        stage_proxy_mesh_.emplace(gpu, stage_proxy_data.mesh_config());
        terrain_runtime_.create(device, {
                                            .shaders = cubey::terrain_backdrop_runtime_shader_files(
                                                CUBEY_TERRAIN_SHADER_DIR),
                                            .frame_slot_count = frame_slot_count,
                                        });
        atmosphere_atlases_.create(device, gpu, {.night_sky_extent = 64U});
        atmosphere_background_.create_materials(
            device,
            {.frame_slot_count = frame_slot_count, .textures = atmosphere_atlases_.bindings()});
        cloud_runtime_.create_surface_resources(device, gpu, cloud_runtime_shader_files().generated,
                                                clouds_config_);
        hdr_post_frame_.create_materials(device, {.frame_slot_count = frame_slot_count});
        global_resources_created_ = true;
    }

    void create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                    VkFormat color_format, std::uint32_t frame_slot_count) {
        scene_depth_.emplace(device, extent, true);
        terrain_target_info_ = cubey::TerrainBackdropRuntimeTargetInfo{
            .extent = extent,
            .color_format = kTerrainSceneColorFormat,
            .depth_format = scene_depth_->format(),
        };
        if (terrain_runtime_.product_ready()) {
            terrain_runtime_.create_target_resources(device, terrain_target_info_.value());
        }

        const std::array stage_proxy_shaders{
            cubey::render::vertex_shader_file(shader_path("terrain_stage_proxy.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("terrain_stage_proxy.frag.spv")),
        };
        const cubey::render::VertexInputLayout stage_proxy_vertex_input =
            cubey::render::vertex_position_color_normal_input_layout();
        const std::array stage_proxy_descriptor_set_layouts{terrain_runtime_.environment_layout()};
        stage_proxy_pipeline_.emplace(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = extent,
                        .color_format = kTerrainSceneColorFormat,
                        .depth_format = scene_depth_->format(),
                        .shader_stage_files = stage_proxy_shaders,
                        .vertex_bindings = stage_proxy_vertex_input.bindings(),
                        .vertex_attributes = stage_proxy_vertex_input.attribute_descriptions(),
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
        terrain_runtime_.destroy_target_resources();
        terrain_target_info_.reset();
        scene_depth_.reset();
    }

    void destroy_all_resources(cubey::vulkan::GpuRuntime& gpu) {
        product_builds_.shutdown(gpu);
        destroy_swapchain_resources();
        hdr_post_frame_.destroy();
        cloud_runtime_.destroy();
        atmosphere_background_.destroy();
        atmosphere_atlases_.shutdown(gpu);
        terrain_runtime_.destroy();
        stage_proxy_mesh_.reset();
        gpu_profiler_.reset();
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
        return terrain_runtime_.shadow_caster_triangle_count();
    }

    void record_metrics(cubey::profiling::ProfileRecorder* recorder,
                        std::uint64_t frame_index) const {
        if (recorder == nullptr) {
            return;
        }
        recorder->record_metric(frame_index, "terrain.backdrop", "submitted_sectors",
                                terrain_runtime_.draw_plan().submitted_sector_count);
        recorder->record_metric(frame_index, "terrain.backdrop", "submitted_triangles",
                                terrain_runtime_.draw_plan().submitted_triangle_count);
        const cubey::StagedResourceStatus& build_status = product_builds_.status();
        recorder->record_metric(frame_index, "terrain.initialization", "generation",
                                static_cast<double>(build_status.generation.id));
        recorder->record_metric(frame_index, "terrain.initialization", "phase",
                                static_cast<double>(static_cast<std::uint8_t>(build_status.phase)));
        recorder->record_metric(frame_index, "terrain.initialization", "prepare_ms",
                                build_status.prepare_milliseconds);
        recorder->record_metric(frame_index, "terrain.initialization", "install_ms",
                                build_status.install_milliseconds);
        recorder->record_metric(frame_index, "terrain.initialization", "resident",
                                terrain_runtime_.product_ready() ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.initialization", "source_load_ms",
                                source_load_milliseconds_);
        recorder->record_metric(frame_index, "terrain.initialization", "climate_load_ms",
                                climate_load_milliseconds_);
        recorder->record_metric(frame_index, "terrain.initialization", "placement_ms",
                                placement_milliseconds_);
        recorder->record_metric(
            frame_index, "terrain.initialization", "product_cache_hit",
            product_cache_diagnostics_.source == TerrainProductPreparationSource::Cache ? 1.0
                                                                                        : 0.0);
        recorder->record_metric(
            frame_index, "terrain.initialization", "product_cache_lookup",
            static_cast<double>(static_cast<std::uint8_t>(product_cache_diagnostics_.lookup)));
        recorder->record_metric(frame_index, "terrain.initialization", "product_cache_stored",
                                product_cache_diagnostics_.stored ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.initialization", "product_cache_load_ms",
                                product_cache_diagnostics_.load_milliseconds);
        recorder->record_metric(frame_index, "terrain.initialization", "product_cache_decode_ms",
                                product_cache_diagnostics_.decode_milliseconds);
        recorder->record_metric(frame_index, "terrain.initialization",
                                "product_cache_generation_ms",
                                product_cache_diagnostics_.generation_milliseconds);
        recorder->record_metric(frame_index, "terrain.initialization", "product_cache_encode_ms",
                                product_cache_diagnostics_.encode_milliseconds);
        recorder->record_metric(frame_index, "terrain.initialization", "product_cache_store_ms",
                                product_cache_diagnostics_.store_milliseconds);
        recorder->record_metric(
            frame_index, "terrain.backdrop", "product_render_triangles",
            static_cast<double>(product_info_.diagnostics.render_triangle_count));
        recorder->record_metric(
            frame_index, "terrain.backdrop", "center_render_triangles",
            static_cast<double>(product_info_.diagnostics.center_render_triangle_count));
        recorder->record_metric(frame_index, "terrain.backdrop", "source_samples",
                                static_cast<double>(product_info_.diagnostics.source_sample_count));
        recorder->record_metric(
            frame_index, "terrain.backdrop", "sampled_vertices",
            static_cast<double>(product_info_.diagnostics.sampled_vertex_count));
        recorder->record_metric(frame_index, "terrain.backdrop", "render_vertices",
                                static_cast<double>(product_info_.diagnostics.render_vertex_count));
        const double retained_vertex_ratio =
            product_info_.diagnostics.sampled_vertex_count == 0U
                ? 0.0
                : static_cast<double>(product_info_.diagnostics.render_vertex_count) /
                      static_cast<double>(product_info_.diagnostics.sampled_vertex_count);
        recorder->record_metric(frame_index, "terrain.backdrop", "vertex_compaction_ratio",
                                retained_vertex_ratio);
        recorder->record_metric(frame_index, "terrain.backdrop", "mesh_upload_bytes",
                                static_cast<double>(terrain_runtime_.mesh_upload_bytes()));
        recorder->record_metric(frame_index, "terrain.backdrop", "mesh_upload_transfer_submissions",
                                terrain_runtime_.mesh_upload_transfer_submissions());
        recorder->record_metric(frame_index, "terrain.backdrop", "content_hash_low32",
                                static_cast<double>(static_cast<std::uint32_t>(
                                    product_info_.diagnostics.content_hash)));
        recorder->record_metric(frame_index, "terrain.backdrop", "content_hash_high32",
                                static_cast<double>(static_cast<std::uint32_t>(
                                    product_info_.diagnostics.content_hash >> 32U)));
        recorder->record_metric(frame_index, "terrain.backdrop", "geometry_hash_low32",
                                static_cast<double>(static_cast<std::uint32_t>(
                                    product_info_.diagnostics.geometry_hash)));
        recorder->record_metric(frame_index, "terrain.backdrop", "geometry_hash_high32",
                                static_cast<double>(static_cast<std::uint32_t>(
                                    product_info_.diagnostics.geometry_hash >> 32U)));
        recorder->record_metric(frame_index, "terrain.surface", "model",
                                static_cast<double>(runtime_config_.surface_model));
        recorder->record_metric(frame_index, "terrain.surface", "mean_rock",
                                product_info_.diagnostics.mean_rock);
        recorder->record_metric(frame_index, "terrain.surface", "mean_snow",
                                product_info_.diagnostics.mean_snow);
        recorder->record_metric(frame_index, "terrain.surface", "mean_vegetation",
                                product_info_.diagnostics.mean_vegetation);
        recorder->record_metric(frame_index, "terrain.surface", "mean_moisture",
                                product_info_.diagnostics.mean_moisture);
        recorder->record_metric(frame_index, "terrain.surface", "climate_bound",
                                climate_source_.has_value() ? 1.0 : 0.0);
        if (climate_source_.has_value()) {
            const std::string_view hash = climate_source_->metadata().climate_sha256;
            recorder->record_metric(frame_index, "terrain.surface", "climate_hash_first32",
                                    static_cast<double>(sha256_word(hash, 0U)));
            recorder->record_metric(frame_index, "terrain.surface", "climate_hash_last32",
                                    static_cast<double>(sha256_word(hash, hash.size() - 8U)));
        }
        const TerrainBackdropClimateDiagnostics& climate = climate_diagnostics_;
        recorder->record_metric(frame_index, "terrain.climate", "sample_count",
                                static_cast<double>(climate.sample_count));
        recorder->record_metric(frame_index, "terrain.climate", "mean_temperature_c",
                                climate.mean_temperature_c);
        recorder->record_metric(frame_index, "terrain.climate", "mean_temperature_stddev_c",
                                climate.mean_temperature_stddev_c);
        recorder->record_metric(frame_index, "terrain.climate", "mean_precipitation_annual_mm",
                                climate.mean_precipitation_annual_mm);
        recorder->record_metric(frame_index, "terrain.climate", "mean_precipitation_cv",
                                climate.mean_precipitation_cv);
        recorder->record_metric(frame_index, "terrain.climate", "mean_growing_season_days",
                                climate.mean_growing_season_days);
        recorder->record_metric(frame_index, "terrain.climate", "mean_thermal_growth",
                                climate.mean_thermal_growth);
        recorder->record_metric(frame_index, "terrain.climate",
                                "mean_thermal_water_demand_proxy_mm",
                                climate.mean_thermal_water_demand_proxy_mm);
        recorder->record_metric(frame_index, "terrain.climate", "mean_climate_moisture_ratio",
                                climate.mean_climate_moisture_ratio);
        recorder->record_metric(frame_index, "terrain.climate", "mean_seasonality_factor",
                                climate.mean_seasonality_factor);
        recorder->record_metric(frame_index, "terrain.climate", "mean_effective_moisture",
                                climate.mean_effective_moisture);
        recorder->record_metric(frame_index, "terrain.climate", "mean_moisture_weight",
                                climate.mean_moisture_weight);
        recorder->record_metric(frame_index, "terrain.climate", "mean_cover_weight",
                                climate.mean_cover_weight);
        recorder->record_metric(frame_index, "terrain.climate", "mean_annual_cold_potential",
                                climate.mean_annual_cold_potential);
        recorder->record_metric(frame_index, "terrain.climate", "mean_wet_snow_potential",
                                climate.mean_wet_snow_potential);
        recorder->record_metric(frame_index, "terrain.backdrop", "render_stride",
                                static_cast<double>(product_info_.request.render_stride));
        recorder->record_metric(frame_index, "terrain.backdrop", "outer_radius_m",
                                product_info_.request.outer_radius_m);
        recorder->record_metric(frame_index, "terrain.placement", "mode",
                                static_cast<double>(placement_stage_.mode));
        recorder->record_metric(frame_index, "terrain.placement", "sample_index",
                                static_cast<double>(placement_stage_.sample_index));
        recorder->record_metric(frame_index, "terrain.placement", "source_focus_x_m",
                                placement_stage_.placement.source_focus_xz.x);
        recorder->record_metric(frame_index, "terrain.placement", "source_focus_z_m",
                                placement_stage_.placement.source_focus_xz.y);
        recorder->record_metric(frame_index, "terrain.placement", "local_contract",
                                placement_stage_.placement.local_contract_satisfied ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.placement", "directional_contract",
                                placement_stage_.placement.contract_satisfied ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.placement", "score",
                                placement_stage_.placement.score);
        recorder->record_metric(frame_index, "terrain.placement", "local_radius_m",
                                placement_stage_.placement.local_radius_m);
        recorder->record_metric(frame_index, "terrain.placement", "local_relief_m",
                                placement_stage_.placement.local_relief_m);
        recorder->record_metric(frame_index, "terrain.placement", "maximum_local_relief_m",
                                placement_stage_.placement.maximum_local_relief_m);
        recorder->record_metric(frame_index, "terrain.placement", "local_p95_slope",
                                placement_stage_.placement.local_p95_slope);
        recorder->record_metric(frame_index, "terrain.placement", "maximum_local_p95_slope",
                                placement_stage_.placement.maximum_local_p95_slope);
        recorder->record_metric(frame_index, "terrain.placement", "baked_clearance_m",
                                placement_stage_.stage.minimum_camera_clearance_m);
        recorder->record_metric(frame_index, "terrain.placement", "foreground_height_m",
                                foreground_height_m_);
        recorder->record_metric(
            frame_index, "terrain.backdrop", "filtered_detail",
            runtime_config_.material == TerrainMaterialMode::FilteredDetail ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.backdrop", "material_texture_bytes",
                                static_cast<double>(terrain_runtime_.material_texture_bytes()));
        recorder->record_metric(frame_index, "terrain.shadow", "enabled",
                                runtime_config_.shadows ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.shadow", "map_extent",
                                static_cast<double>(cubey::render::kTerrainShadowMapExtent));
        recorder->record_metric(frame_index, "terrain.shadow", "texel_world_m",
                                terrain_runtime_.shadow_projection().texel_world_size_m);
        recorder->record_metric(frame_index, "terrain.shadow", "depth_span_m",
                                terrain_runtime_.shadow_projection().depth_span_m);
        recorder->record_metric(frame_index, "terrain.shadow", "triangle_count",
                                static_cast<double>(shadow_caster_triangle_count()));
        recorder->record_metric(frame_index, "terrain.shadow", "cache_valid",
                                terrain_runtime_.shadow_cache().valid ? 1.0 : 0.0);
        recorder->record_metric(frame_index, "terrain.shadow", "update_count",
                                static_cast<double>(terrain_runtime_.shadow_cache().update_count));
        recorder->record_metric(frame_index, "terrain.shadow", "updated",
                                terrain_runtime_.shadow_update_this_frame() ? 1.0 : 0.0);
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
        terrain_runtime_.record_shadow_pass(recorder);
    }

    void record_terrain_pass(const cubey::vulkan::CommandRecorder& recorder,
                             cubey::render::ColorTargetView color,
                             cubey::render::DepthTargetView depth,
                             cubey::render::FrameSlot frame_slot) const {
        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(color, depth);
        const cubey::render::RenderTargetRenderingInfo rendering(
            target,
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                .depth = cubey::render::depth_clear_value(),
            },
            cubey::render::RenderTargetAttachmentOps{
                .color = cubey::vulkan::load_store_attachment_ops(),
                .depth = cubey::vulkan::clear_discard_attachment_ops(),
            });
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(color.extent);
        terrain_runtime_.record_surface_draws(recorder, frame_slot);
        recorder.end_rendering();
    }

    void record_stage_proxy_pass(const cubey::vulkan::CommandRecorder& recorder,
                                 cubey::render::ColorTargetView color,
                                 cubey::render::DepthTargetView depth,
                                 cubey::render::FrameSlot frame_slot, bool clear_depth) const {
        const cubey::render::RenderTargetView target =
            cubey::render::render_target_view(color, depth);
        const cubey::render::RenderTargetRenderingInfo rendering(
            target,
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                .depth = cubey::render::depth_clear_value(),
            },
            cubey::render::RenderTargetAttachmentOps{
                .color = cubey::vulkan::load_store_attachment_ops(),
                .depth = clear_depth ? cubey::vulkan::clear_discard_attachment_ops()
                                     : cubey::vulkan::load_store_attachment_ops(),
            });
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(color.extent);
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, stage_proxy_pipeline().pipeline());
        terrain_runtime_.bind_environment(recorder, stage_proxy_pipeline(), frame_slot);
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

    static void record_placeholder_depth_pass(const cubey::vulkan::CommandRecorder& recorder,
                                              cubey::render::ColorTargetView color,
                                              cubey::render::DepthTargetView depth) {
        const cubey::render::RenderTargetRenderingInfo rendering(
            cubey::render::render_target_view(color, depth),
            cubey::render::RenderClearValues{
                .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                .depth = cubey::render::depth_clear_value(),
            },
            cubey::render::RenderTargetAttachmentOps{
                .color = cubey::vulkan::load_store_attachment_ops(),
                .depth = cubey::vulkan::clear_discard_attachment_ops(),
            });
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(color.extent);
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
        const cubey::render::RenderGraphTextureHandle depth = graph.import_depth_target(
            "terrain depth", cubey::render::depth_target_view(*scene_depth_),
            cubey::render::render_graph_undefined_texture_state());
        const bool terrain_ready = terrain_runtime_.target_resources_created();
        std::optional<cubey::render::RenderGraphTextureHandle> shadow_depth;
        if (terrain_ready) {
            const std::optional<cubey::render::RenderGraphTextureState> shadow_initial_state =
                terrain_runtime_.shadow_depth_is_sampled()
                    ? cubey::render::render_graph_sampled_depth_texture_state()
                    : cubey::render::render_graph_undefined_texture_state();
            shadow_depth = graph.import_depth_target("terrain shadow depth",
                                                     terrain_runtime_.shadow_depth_target(),
                                                     shadow_initial_state);

            if (terrain_runtime_.shadow_update_this_frame()) {
                graph.add_pass("terrain shadow", cubey::render::RenderGraphQueueDomain::Graphics)
                    .write_depth(shadow_depth.value())
                    .material_pass(terrain_runtime_.shadow_material_pass())
                    .execute([this](const cubey::render::RenderGraphExecutionContext& context) {
                        record_shadow_pass(context.recorder());
                    });
            }
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
        if (terrain_ready) {
            graph.add_pass("terrain surface", cubey::render::RenderGraphQueueDomain::Graphics)
                .read_texture(shadow_depth.value())
                .write_color(scene_color)
                .write_depth(depth)
                .material_pass(terrain_runtime_.surface_material_pass())
                .execute([this, scene_color, depth,
                          frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                    record_terrain_pass(
                        context.recorder(),
                        cubey::render::resolved_color_target_view(context, scene_color),
                        cubey::render::resolved_depth_target_view(context, depth), frame_slot);
                });
        }
        if (runtime_config_.foreground_sphere) {
            graph.add_pass("terrain stage proxy", cubey::render::RenderGraphQueueDomain::Graphics)
                .write_color(scene_color)
                .write_depth(depth)
                .material_pass(terrain_stage_proxy_pass_info())
                .execute([this, scene_color, depth, frame_slot, terrain_ready](
                             const cubey::render::RenderGraphExecutionContext& context) {
                    record_stage_proxy_pass(
                        context.recorder(),
                        cubey::render::resolved_color_target_view(context, scene_color),
                        cubey::render::resolved_depth_target_view(context, depth), frame_slot,
                        !terrain_ready);
                });
        } else if (!terrain_ready) {
            graph
                .add_pass("terrain placeholder depth",
                          cubey::render::RenderGraphQueueDomain::Graphics)
                .write_color(scene_color)
                .write_depth(depth)
                .execute([scene_color,
                          depth](const cubey::render::RenderGraphExecutionContext& context) {
                    record_placeholder_depth_pass(
                        context.recorder(),
                        cubey::render::resolved_color_target_view(context, scene_color),
                        cubey::render::resolved_depth_target_view(context, depth));
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
        if (terrain_runtime_.target_resources_created()) {
            terrain_runtime_.complete_frame();
        }
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
        if (terrain_runtime_.target_resources_created()) {
            terrain_runtime_.prepare_frame(
                frame_slot, {
                                .view_projection = camera_.view_projection_matrix(
                                    frame_camera_transform_, aspect(target.extent)),
                                .camera_position = frame_camera_transform_.translation,
                                .atmosphere = atmosphere_frame,
                                .lighting = lighting,
                                .debug_view = runtime_config_.debug_view,
                                .material = runtime_config_.material,
                                .aerial_perspective_strength =
                                    runtime_config_.aerial_perspective_strength,
                                .shadows_enabled = runtime_config_.shadows,
                            });
        } else {
            terrain_runtime_.prepare_environment(frame_slot, atmosphere_frame, lighting);
        }
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

    [[nodiscard]] const cubey::render::Mesh& stage_proxy_mesh() const {
        if (!stage_proxy_mesh_.has_value()) {
            throw std::runtime_error("terrain stage proxy mesh is not initialized");
        }
        return stage_proxy_mesh_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& stage_proxy_pipeline() const {
        if (!stage_proxy_pipeline_.has_value()) {
            throw std::runtime_error("terrain stage proxy pipeline is not initialized");
        }
        return stage_proxy_pipeline_.value();
    }

    RunConfig run_config_;
    TerrainRuntimeConfig runtime_config_{};
    TerrainRuntimeConfig startup_runtime_config_{};
    cubey::procedural::ProceduralArtifactCache product_cache_;
    std::atomic<TerrainProductLoadPhase> product_load_phase_{TerrainProductLoadPhase::None};
    std::optional<Clock::time_point> product_load_started_at_{};
    cubey::jobs::JobSystem product_jobs_{1U};
    cubey::TerrainBackdropRuntime terrain_runtime_{};
    cubey::StagedResource<TerrainProductBuild, TerrainResidentBuild> product_builds_;
    std::optional<TerrainRasterHeightSource> source_{};
    std::optional<TerrainRasterClimateSource> climate_source_{};
    TerrainBackdropPlacementPlan placement_stage_{};
    TerrainBackdropClimateDiagnostics climate_diagnostics_{};
    TerrainProductCacheDiagnostics product_cache_diagnostics_{};
    TerrainBackdropProductInfo product_info_{};
    std::vector<TerrainSourceChoice> source_choices_{};
    std::size_t source_choice_index_ = 0U;
    std::size_t edit_source_choice_index_ = 0U;
    std::vector<TerrainSourceAvailability> source_choice_availability_{};
    std::vector<bool> source_choice_climate_available_{};
    std::string preset_catalog_error_{};
    TerrainPlacementMode edit_placement_mode_ = TerrainPlacementMode::Selected;
    std::uint32_t edit_placement_index_ = 0U;
    TerrainSurfaceModel edit_surface_model_ = TerrainSurfaceModel::MineralControl;
    std::string product_rebuild_error_{};
    double source_load_milliseconds_ = 0.0;
    double climate_load_milliseconds_ = 0.0;
    double placement_milliseconds_ = 0.0;
    cubey::OrbitController orbit_controller_;
    cubey::Camera3D camera_;
    cubey::Transform3D frame_camera_transform_{};
    cubey::AtmosphereEnvironmentRunState atmosphere_state_{};
    cubey::CloudEnvironmentConfig clouds_config_{};
    cubey::CloudEnvironmentRuntime cloud_runtime_{};
    float baked_foreground_height_m_ = 500.0F;
    float foreground_height_m_ = kTerrainDefaultForegroundHeightM;

    std::optional<cubey::render::Mesh> stage_proxy_mesh_{};
    std::optional<cubey::vulkan::DepthAttachment> scene_depth_{};
    std::optional<cubey::TerrainBackdropRuntimeTargetInfo> terrain_target_info_{};
    cubey::AtmosphereBackgroundAtlasRuntime atmosphere_atlases_{};
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_{};
    cubey::render::HdrPostFrame hdr_post_frame_{};
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
