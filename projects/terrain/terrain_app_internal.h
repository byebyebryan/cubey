#pragma once

#include "terrain_app.h"

#include "terrain_capture_preparation.h"
#include "terrain_config.h"
#include "terrain_landscape_presentation.h"
#include "terrain_product_adapter.h"
#include "terrain_source_catalog.h"
#include <cubey/asset/terrain_landscape_catalog.h>

#include <cubey/asset/terrain_raster_height_source.h>
#include <cubey/core/jobs.h>
#include <cubey/engine/atmosphere_background_atlas_runtime.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/cloud_environment_runtime.h>
#include <cubey/engine/staged_resource.h>
#include <cubey/engine/terrain_backdrop_runtime.h>
#include <cubey/engine/terrain_water_capture_runtime.h>
#include <cubey/host/atmosphere_environment_ui.h>
#include <cubey/host/cloud_environment_ui.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/imgui_helpers.h>
#include <cubey/host/terrain_surface_environment_ui.h>
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
#include <cubey/vulkan/vk_check.h>

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
#include <sstream>
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
namespace detail {

using Clock = std::chrono::steady_clock;
using cubey::asset::TerrainRasterProvenance;
using cubey::terrain::plan_terrain_backdrop_placement;
using cubey::terrain::TerrainBackdropPlacementPlan;
using cubey::terrain::TerrainBackdropPlacementRequest;
using cubey::terrain::TerrainBackdropProduct;
using cubey::terrain::TerrainBackdropProductInfo;
using cubey::terrain::TerrainBackdropStagePlan;
using cubey::terrain::TerrainDirectionalPlacementPlan;

inline constexpr VkFormat kTerrainSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr float kTerrainHeadlessOrbitSpeed = 0.18F;
inline constexpr std::uint32_t kTerrainGpuProfilerPassCapacity = 16U;
inline constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
inline constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
inline constexpr float kTerrainMinimumForegroundHeightM = 2.0F;
inline constexpr float kTerrainDefaultForegroundHeightM = 200.0F;
inline constexpr float kTerrainMaximumForegroundHeightM = 1'000.0F;
inline constexpr float kTerrainCloudSceneDepthFadeM = 500.0F;
inline constexpr float kTerrainInspectionPitchLimitRadians = 1'000.0F;
inline constexpr float kTerrainInspectionMaximumOrbitRadiusM = 1'000.0F;

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
                                    const std::filesystem::path& rhs);

[[nodiscard]] std::vector<TerrainSourceChoice>
terrain_source_choices(const TerrainRuntimeConfig& startup_config,
                       std::string& preset_catalog_error);

[[nodiscard]] bool terrain_manifest_available(const std::filesystem::path& path,
                                              std::string_view manifest);

[[nodiscard]] bool terrain_source_climate_available(const TerrainSourcePaths& paths);

[[nodiscard]] TerrainSourceAvailability
terrain_source_choice_availability(const TerrainSourceChoice& choice);

[[nodiscard]] std::string terrain_source_choice_label(const TerrainSourceChoice& choice,
                                                      TerrainSourceAvailability availability);

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

inline constexpr std::array<std::string_view, 8U> kTerrainGpuTimingLabels{
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

[[nodiscard]] std::uint64_t profile_frame_index(std::uint64_t frame_index);

[[nodiscard]] double elapsed_milliseconds(Clock::time_point started);

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
                                                          cubey::render::FrameSlot frame_slot);

[[nodiscard]] std::uint32_t sha256_word(std::string_view hash, std::size_t offset) noexcept;

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings);

void draw_terrain_gpu_timings(std::span<const cubey::vulkan::GpuPassTiming> timings);

[[nodiscard]] std::filesystem::path shader_path(std::filesystem::path filename);

[[nodiscard]] cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files();

[[nodiscard]] std::filesystem::path
require_heightfield(const std::filesystem::path& heightfield_path);

[[nodiscard]] std::optional<TerrainRasterClimateSource>
load_climate_source(const TerrainRuntimeConfig& config, const TerrainRasterHeightSource& source);

[[nodiscard]] TerrainBackdropPlacementPlan
make_placement_stage(const TerrainRasterHeightSource& source, const TerrainRuntimeConfig& config);

[[nodiscard]] std::uint64_t
terrain_product_placement_parameter_hash(const TerrainRuntimeConfig& config,
                                         const TerrainBackdropPlacementPlan& placement);

[[nodiscard]] cubey::render::MaterialPassInfo terrain_stage_proxy_pass_info();

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>
terrain_stage_proxy_mesh_data();

[[nodiscard]] std::string_view short_revision(std::string_view revision);

[[nodiscard]] TerrainBackdropPlacementPlan loading_placement_stage();

[[nodiscard]] TerrainProjectConfig resolve_terrain_app_config(TerrainProjectConfig config);

class TerrainApp {
    struct LandscapeBuild {
        TerrainProjectConfig config;
        std::unique_ptr<cubey::TerrainWaterCaptureRuntime> runtime;
        VkDeviceSize observed_install_heap_bytes = 0;
    };

  public:
    explicit TerrainApp(TerrainProjectConfig config)
        : config_(resolve_terrain_app_config(std::move(config))), runtime_config_(config_.runtime),
          startup_runtime_config_(runtime_config_),
          product_cache_({.root = cubey::procedural::default_procedural_artifact_cache_root()}),
          product_builds_(product_jobs_), landscape_builds_(product_jobs_),
          placement_stage_(loading_placement_stage()),
          orbit_controller_(cubey::OrbitControllerConfig{
              .min_pitch = -kTerrainInspectionPitchLimitRadians,
              .max_pitch = kTerrainInspectionPitchLimitRadians,
          }),
          camera_(cubey::Camera3DConfig{
              .fovy_radians = 40.0F * kDegreesToRadians,
              .near_z = 0.1F,
              .far_z = 100'000.0F,
          }),
          atmosphere_state_(terrain_atmosphere_state_from_options(config_.atmosphere)),
          clouds_config_(
              terrain_cloud_config_from_options(config_.clouds, atmosphere_state_.environment)) {
        source_choices_ = terrain_source_choices(startup_runtime_config_, preset_catalog_error_);
        source_choice_availability_.resize(source_choices_.size());
        source_choice_climate_available_.resize(source_choices_.size());
        refresh_source_choice_availability();
        edit_placement_mode_ = runtime_config_.placement;
        edit_placement_index_ = runtime_config_.placement_index;
        edit_surface_model_ = runtime_config_.surface_model;
        edit_source_choice_index_ = source_choice_index_;
        try {
            landscape_catalog_ = config_.landscape_catalog.value_or(
                cubey::asset::load_terrain_landscape_catalog(config_.landscape_catalog_path));
            refresh_landscape_availability();
            for (std::size_t i = 0; i < landscape_catalog_->presets.size(); ++i)
                if (landscape_catalog_->presets[i].id == config_.active_landscape_id)
                    edit_landscape_index_ = i;
        } catch (const std::exception& error) {
            landscape_error_ = error.what();
        }
        if (!config_.landscape_startup_notice.empty())
            std::printf("terrain_landscape_startup: %s\n",
                        config_.landscape_startup_notice.c_str());
        if (config_.water_capture_path || config_.terrain_site) {
            try {
                capture_runtime_ = prepare_terrain_capture(config_, CUBEY_TERRAIN_SHADER_DIR);
                capture_active_ = config_.water_capture_view == "captured-patch";
                capture_animation_s_ = config_.captured_water_animation_s;
                configure_capture_camera();
                log_landscape_presentation();
            } catch (const std::exception& error) {
                // Explicit headless requests fail loudly; interactive users can
                // still use the normal source picker/backdrop after rejection.
                if (config_.common.headless || !config_.active_landscape_id.empty())
                    throw;
                capture_runtime_.reset();
                capture_active_ = false;
                capture_error_ = error.what();
            }
        }
        configure_camera_for_placement(true);
        if (!capture_active_)
            request_product_build(runtime_config_, source_choice_index_);
    }

    TerrainApp(const TerrainApp&) = delete;
    TerrainApp& operator=(const TerrainApp&) = delete;

    int run();

  private:
    void configure_capture_camera();
    int run_windowed();

    int run_headless();

    void refresh_source_choice_availability();

    void refresh_landscape_availability();

    void request_landscape(const std::string& id);

    void poll_landscape_build(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                              cubey::vulkan::GpuSubmissionTicket retire_after);

    void log_landscape_presentation() const;

    void draw_landscape_selector();

    void draw_ui(cubey::host::WindowedAppContext& context);

    void configure_camera_for_placement(bool reset_foreground_height);

    void reset_inspection_camera();

    void start_product_build();

    void request_product_build(TerrainRuntimeConfig config,
                               std::size_t requested_source_choice_index);

    void install_product_build(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                               cubey::vulkan::GpuSubmissionTicket retire_after,
                               TerrainResidentBuild installed);

    void install_ready_product_build(const cubey::vulkan::Device& device,
                                     cubey::vulkan::GpuRuntime& gpu,
                                     cubey::vulkan::GpuSubmissionTicket retire_after);

    void poll_product_build(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                            cubey::vulkan::GpuSubmissionTicket retire_after);

    void poll_atmosphere_atlases(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu,
                                 const cubey::vulkan::FrameResources& frame_resources,
                                 cubey::vulkan::GpuSubmissionTicket retire_after);

    void finish_atmosphere_atlases(const cubey::vulkan::Device& device,
                                   cubey::vulkan::GpuRuntime& gpu);

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count);

    void create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                    VkFormat color_format, std::uint32_t frame_slot_count);
    void prepare_capture_context(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu);

    void destroy_swapchain_resources();

    void destroy_all_resources(cubey::vulkan::GpuRuntime& gpu);

    [[nodiscard]] const std::vector<cubey::vulkan::GpuPassTiming>& latest_gpu_timings() const;

    [[nodiscard]] cubey::vulkan::GpuTimestampProfiler* gpu_profiler();

    void collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                             std::uint64_t frame_index, cubey::render::FrameSlot frame_slot);

    [[nodiscard]] std::uint64_t shadow_caster_triangle_count() const noexcept;

    void record_metrics(cubey::profiling::ProfileRecorder* recorder,
                        std::uint64_t frame_index) const;

    [[nodiscard]] cubey::Transform3D current_camera_transform() const;

    [[nodiscard]] float foreground_vertical_offset_m() const noexcept;

    [[nodiscard]] static float aspect(VkExtent2D extent);

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_uniforms(VkExtent2D extent) const;

    [[nodiscard]] bool cloud_layer_enabled() const noexcept;

    [[nodiscard]] cubey::CloudEnvironmentRuntimeFrame
    current_cloud_frame(VkExtent2D extent,
                        const cubey::render::AtmosphereEnvironmentLighting& lighting) const;

    [[nodiscard]] float display_exposure() const;

    void record_shadow_pass(const cubey::vulkan::CommandRecorder& recorder) const;

    void record_terrain_pass(const cubey::vulkan::CommandRecorder& recorder,
                             cubey::render::ColorTargetView color,
                             cubey::render::DepthTargetView depth,
                             cubey::render::FrameSlot frame_slot) const;

    void record_stage_proxy_pass(const cubey::vulkan::CommandRecorder& recorder,
                                 cubey::render::ColorTargetView color,
                                 cubey::render::DepthTargetView depth,
                                 cubey::render::FrameSlot frame_slot, bool clear_depth) const;

    static void record_placeholder_depth_pass(const cubey::vulkan::CommandRecorder& recorder,
                                              cubey::render::ColorTargetView color,
                                              cubey::render::DepthTargetView depth);

    [[nodiscard]] CompiledTerrainGraph current_render_graph(
        cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
        cubey::render::RenderGraphTextureState target_initial_state,
        cubey::render::RenderGraphTextureState target_final_state,
        const std::optional<cubey::CloudEnvironmentRuntimeFrame>& cloud_runtime_frame) const;

    void complete_shadow_frame_recording();

    void record_target(const cubey::vulkan::Device& device, VkCommandBuffer command_buffer,
                       cubey::render::ColorTargetView target, cubey::render::FrameSlot frame_slot,
                       cubey::render::RenderGraphTextureState target_initial_state,
                       cubey::render::RenderGraphTextureState target_final_state,
                       cubey::render::RenderGraphCommandBufferMode command_buffer_mode);

    [[nodiscard]] const cubey::render::Mesh& stage_proxy_mesh() const;

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& stage_proxy_pipeline() const;

    std::unique_ptr<cubey::TerrainWaterCaptureRuntime> capture_runtime_;
    cubey::vulkan::GpuRuntime* gpu_ = nullptr;
    bool capture_active_ = false;
    bool backdrop_requested_ = false;
    std::string capture_error_;
    std::vector<bool> capture_profile_slots_;
    double capture_animation_s_ = 0.0;
    double capture_review_s_ = 0.0;
    cubey::OrbitController capture_orbit_;
    cubey::Camera3D capture_camera_;
    cubey::math::Vec3 capture_target_{};
    float capture_yaw_ = -0.52F, capture_pitch_ = -0.92F;
    VkFormat capture_color_format_ = VK_FORMAT_UNDEFINED;
    TerrainProjectConfig config_;
    TerrainRuntimeConfig runtime_config_{};
    TerrainRuntimeConfig startup_runtime_config_{};
    cubey::procedural::ProceduralArtifactCache product_cache_;
    std::atomic<TerrainProductLoadPhase> product_load_phase_{TerrainProductLoadPhase::None};
    std::optional<Clock::time_point> product_load_started_at_{};
    cubey::jobs::JobSystem product_jobs_{1U};
    cubey::TerrainBackdropRuntime terrain_runtime_{};
    cubey::StagedResource<TerrainProductBuild, TerrainResidentBuild> product_builds_;
    cubey::StagedResource<LandscapeBuild, LandscapeBuild> landscape_builds_;
    std::optional<cubey::asset::TerrainLandscapeCatalog> landscape_catalog_;
    std::vector<cubey::asset::TerrainLandscapeProbe> landscape_availability_;
    std::size_t edit_landscape_index_ = 0;
    std::vector<std::string> landscape_smoke_ids_;
    std::string landscape_error_;
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

} // namespace detail
} // namespace cubey::projects::terrain
