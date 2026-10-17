#pragma once

#include "ocean_app.h"

#include "ocean_config.h"
#include "ocean_gpu_resources.h"
#include "ocean_mesh.h"
#include "ocean_spectrum_diagnostics.h"
#include "ocean_surface_frame.h"
#include "ocean_surface_quality.h"
#include "ocean_ui.h"

#include <cubey/core/math.h>
#include <cubey/core/profiling.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/engine/cloud_environment_runtime.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/input.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/cloud_layer.h>
#include <cubey/render/cloud_planar_reflection.h>
#include <cubey/render/color_space.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pbr.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>
#include <cubey/render/terrain_ocean_fields.h>
#include <cubey/render/uniform_buffer.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/memory_barriers.h>
#include <cubey/vulkan/sampler.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef CUBEY_OCEAN_SHADER_DIR
#error "CUBEY_OCEAN_SHADER_DIR must be defined by the ocean CMake target"
#endif

namespace cubey::projects::ocean {
namespace detail {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

inline constexpr float kCameraDistance = 125.0F;
inline constexpr float kCameraMinDistance = 18.0F;
inline constexpr float kCameraMaxDistance = 8000.0F;
inline constexpr float kCameraBaseYaw = cubey::render::kAtmosphereEnvironmentSunriseViewYawRadians;
inline constexpr float kCameraBasePitch = cubey::render::kAtmosphereEnvironmentSunriseViewPitchRadians;
inline constexpr float kCameraNearPlane = 0.25F;
inline constexpr float kCameraFarPlane = 14000.0F;
inline constexpr float kCameraMinimumSurfaceClearanceMeters = 8.0F;
inline constexpr VkFormat kOceanSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat kOceanDepthFormat = VK_FORMAT_D32_SFLOAT;
inline constexpr float kOceanSunElevationDegrees = 20.0F;
inline constexpr float kOceanSunAzimuthDegrees = -20.0F;
inline constexpr float kReferencePillarHalfWidthMeters = 0.50F;
inline constexpr float kReferencePillarCenterXMeters = -24.0F;
inline constexpr float kReferencePillarCenterZMeters = 10.0F;
inline constexpr float kReferencePillarAxisUX = 0.70710678F;
inline constexpr float kReferencePillarAxisUZ = 0.70710678F;
inline constexpr float kReferencePillarAxisVX = -0.70710678F;
inline constexpr float kReferencePillarAxisVZ = 0.70710678F;
inline constexpr float kReferencePillarMinYMeters = -25.0F;
inline constexpr float kReferencePillarMaxYMeters = 25.0F;
inline constexpr float kReferencePillarMeterMarkerHalfHeightMeters = 0.055F;
inline constexpr float kReferencePillarFiveMeterMarkerHalfHeightMeters = 0.22F;
inline constexpr float kReferencePillarTenMeterMarkerHalfHeightMeters = 0.34F;
inline constexpr float kReferencePillarMarkerHalfWidthMeters = kReferencePillarHalfWidthMeters + 0.012F;

struct OceanPushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Vec4 camera_time;
    cubey::math::Vec4 mesh_options;
    cubey::math::Vec4 patch_bounds;
    cubey::math::Vec4 debug_options;
    cubey::math::Vec4 inspection_options;
    cubey::math::Vec4 tile_lengths;
    cubey::math::Vec4 displacement_scales;
    cubey::math::Vec4 normal_scales;
    cubey::math::Vec4 cascade4_options;
    cubey::math::Vec4 water_color;
    cubey::math::Vec4 foam_color;
    cubey::math::Vec4 quality_options;
};

struct OceanSpectrumPushConstants {
    cubey::math::Vec4 seed_tile;
    cubey::math::Vec4 spectrum_options;
    cubey::math::Vec4 shape_options;
    cubey::math::Vec4 cascade_options;
};

struct OceanTerrainFieldUniforms {
    cubey::math::Vec4 uv_transform;
    cubey::math::Vec4 ranges_flags;
};

struct OceanModulatePushConstants {
    cubey::math::Vec4 tile_depth_time;
    cubey::math::Vec4 cascade_options;
};

struct OceanFftPushConstants {
    cubey::math::Vec4 fft_options;
    cubey::math::Vec4 pass_options;
};

struct OceanUnpackPushConstants {
    cubey::math::Vec4 foam_options;
    cubey::math::Vec4 cascade_options;
};

struct OceanReferencePillarPushConstants {
    cubey::math::Mat4 view_projection;
    cubey::math::Vec4 light_direction;
};

static_assert(sizeof(OceanPushConstants) == sizeof(float) * 64U);
static_assert(sizeof(OceanSpectrumPushConstants) == sizeof(float) * 16U);
static_assert(sizeof(OceanTerrainFieldUniforms) == sizeof(float) * 8U);
static_assert(sizeof(OceanModulatePushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanFftPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanUnpackPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanReferencePillarPushConstants) == sizeof(float) * 20U);

enum class OceanRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

struct OceanFrameGraph {
    cubey::render::CompiledRenderGraph graph;
    cubey::render::RenderGraphTextureHandle backbuffer{};
    cubey::render::RenderGraphTextureHandle post_scene_color{};
    cubey::render::RenderGraphTextureHandle scene_color{};
    cubey::render::RenderGraphTextureHandle surface_depth{};
    cubey::render::RenderGraphTextureHandle cloud_scene_color{};
    cubey::render::CloudLayerRuntimeFrame cloud{};
    cubey::render::CloudLayerShadowProduct cloud_shadow{};
    bool cloud_product_enabled = false;
    bool cloud_shadow_enabled = false;
    bool cloud_composite_enabled = false;
};

struct OceanMeshDrawPlan {
    OceanSurfaceFrame surface_frame{};
    OceanMeshPatchList patches{};
    std::array<bool, kOceanMaxMeshPatches> visible_patches{};
    std::array<OceanPatchShadingPlan, kOceanMaxMeshPatches> shading_plans{};
    OceanMeshDrawStats stats{};
};

struct OceanCameraPresetConfig {
    OceanCameraPreset preset = OceanCameraPreset::Default;
    float distance = kCameraDistance;
    float yaw = kCameraBaseYaw;
    float pitch = kCameraBasePitch;
};

[[nodiscard]] std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                                          cubey::render::FrameSlot frame_slot);

[[nodiscard]] float ocean_patch_wave_cull_margin_m(const OceanConfig& config);

[[nodiscard]] cubey::Bounds3D ocean_mesh_patch_world_bounds(const OceanConfig& config,
                                                            const OceanSurfaceFrame& surface_frame,
                                                            const OceanMeshPatch& patch,
                                                            cubey::math::Vec3 camera_position_m);

void record_gpu_timings(cubey::profiling::ProfileRecorder* recorder, std::uint64_t frame_index,
                        const std::vector<cubey::vulkan::GpuPassTiming>& timings);

[[nodiscard]] std::string ocean_gpu_timing_label(const char* phase, std::uint32_t cascade);

[[nodiscard]] cubey::render::PrimitiveVec3 linear_srgb(cubey::render::PrimitiveVec3 srgb_color);

[[nodiscard]] cubey::render::PrimitiveVec3 pillar_position(float local_u, float y, float local_v);

[[nodiscard]] cubey::render::PrimitiveVec3 pillar_u_normal(float sign);

[[nodiscard]] cubey::render::PrimitiveVec3 pillar_v_normal(float sign);

void append_pillar_quad(
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>& mesh,
    cubey::render::PrimitiveVec3 p0, cubey::render::PrimitiveVec3 p1,
    cubey::render::PrimitiveVec3 p2, cubey::render::PrimitiveVec3 p3,
    cubey::render::PrimitiveVec3 normal, cubey::render::PrimitiveVec3 color);

void append_pillar_band(
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>& mesh,
    float half_width, float min_y, float max_y, cubey::render::PrimitiveVec3 color, bool bottom_cap,
    bool top_cap);

[[nodiscard]] cubey::render::PrimitiveVec3 reference_pillar_marker_color(int meter);

[[nodiscard]] float reference_pillar_marker_half_height(int meter);

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>
make_ocean_reference_pillar_mesh();

[[nodiscard]] cubey::render::MaterialPassInfo ocean_reference_pillar_pass_info();

[[nodiscard]] float radians(float degrees);

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
ocean_atmosphere_run_state(const OceanStartupOptions& run_config);

[[nodiscard]] cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files();

[[nodiscard]] std::uint32_t log2_exact(std::uint32_t value);

[[nodiscard]] cubey::render::ComputeDispatchGroups ocean_dispatch_groups(const OceanConfig& config,
                                                                         std::uint32_t cascade);

[[nodiscard]] cubey::render::RenderGraphTextureDesc
ocean_depth_texture_desc(const char* label, VkExtent2D extent, VkFormat format);

[[nodiscard]] OceanCameraPresetConfig ocean_camera_preset_config(OceanCameraPreset preset);

[[nodiscard]] OceanCameraPreset ocean_camera_preset_from_name(std::string_view name);

[[nodiscard]] bool ocean_resource_layout_changed(const OceanConfig& lhs, const OceanConfig& rhs);

[[nodiscard]] bool ocean_terrain_field_source_changed(const OceanConfig& lhs,
                                                      const OceanConfig& rhs);

[[nodiscard]] bool ocean_wave_source_changed(const OceanConfig& lhs, const OceanConfig& rhs);

[[nodiscard]] bool ocean_cloud_environment_resource_changed(const OceanConfig& lhs,
                                                            const OceanConfig& rhs);

[[nodiscard]] bool ocean_cloud_planar_resource_changed(const OceanConfig& lhs,
                                                       const OceanConfig& rhs);

[[nodiscard]] std::uint32_t ocean_enabled_cascade_mask(const OceanConfig& config);

[[nodiscard]] bool ocean_should_update_cascade(const OceanConfig& config, std::uint32_t cascade,
                                               std::uint64_t frame_index, bool first_update);

[[nodiscard]] OceanTerrainFieldUniforms
ocean_terrain_field_uniforms(const cubey::render::TerrainOceanPackedFields& fields, bool enabled);

[[nodiscard]] cubey::render::Texture2D
create_ocean_cloud_shadow_fallback_texture(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu);

[[nodiscard]] cubey::render::Texture2D
create_ocean_cloud_reflection_fallback_texture(const cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu);

[[nodiscard]] cubey::render::TerrainOceanPackedFields
make_ocean_diagnostic_terrain_fields(const OceanConfig& config, float water_datum_m);

class OceanApp {
  public:
    explicit OceanApp(OceanProjectConfig config)
        : config_(std::move(config)), ocean_config_(ocean_config_from_options(config_)),
          spectrum_diagnostics_(ocean_spectrum_diagnostics(ocean_config_)),
          clouds_config_(ocean_cloud_config_from_options(config_)),
          atmosphere_state_(ocean_atmosphere_run_state(config_)),
          atmosphere_lighting_(
              cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment)),
          render_view_(ocean_config_.render_view) {
        diagnostics_.selected_cascade = config_.ocean.cascade.value_or(-1);
        diagnostics_.wire_overlay = config_.ocean.wire_overlay;
        if (config_.ocean.size_reference) {
            diagnostics_.size_reference_enabled = *config_.ocean.size_reference;
        }
        if (config_.ocean.wire_opacity) {
            diagnostics_.wire_opacity = *config_.ocean.wire_opacity;
        }
        camera_preset_ =
            ocean_camera_preset_from_name(config_.ocean.camera_preset.value_or("default"));
        camera_.set_projection(camera_.fovy_radians(), kCameraNearPlane, kCameraFarPlane);
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_distance_limits(kCameraMinDistance, kCameraMaxDistance);
        const float orbit_degrees_per_second =
            config_.capture.video_orbit_degrees.has_value()
                ? 0.0F
                : config_.ocean.camera_orbit_spin_degrees_per_second.value_or(0.0F);
        orbit_controller_.set_auto_rotation_speed(orbit_degrees_per_second *
                                                  std::numbers::pi_v<float> / 180.0F);
        apply_camera_preset(camera_preset_);
    }

    OceanApp(const OceanApp&) = delete;
    OceanApp& operator=(const OceanApp&) = delete;

    ~OceanApp() {
        destroy_swapchain_resources();
    }

    int run();

  private:
    int run_windowed();

    int run_headless();

    void update_windowed(cubey::host::WindowedAppContext& context, const FrameTiming& timing);

    void refresh_atmosphere_lighting();

    void update_atmosphere_time(double delta_seconds);

    std::optional<FrameStatsSample> record_frame_stats(VkExtent2D extent,
                                                       const FrameTiming& timing);

    void record_ocean_profile_metrics(cubey::profiling::ProfileRecorder* recorder,
                                      std::uint64_t frame_index, VkExtent2D extent) const;

    void collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                             std::uint64_t frame_index, cubey::render::FrameSlot frame_slot,
                             double elapsed_seconds);

    void maybe_print_gpu_timings(double elapsed_seconds);

    void create_pipeline(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                         VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count = 1U);

    void create_terrain_ocean_field_resources(cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu,
                                              std::uint32_t frame_slot_count);

    void upload_terrain_ocean_field_uniform(cubey::render::FrameSlot frame_slot) const;

    void update_terrain_ocean_field_uniform_descriptors(const cubey::vulkan::Device& device);

    void create_atmosphere_environment_runtime(cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu,
                                               std::uint32_t frame_slot_count);

    void create_atmosphere_background_frame(const cubey::vulkan::Device& device, VkExtent2D extent,
                                            std::uint32_t frame_slot_count);

    [[nodiscard]] cubey::CloudEnvironmentConfig
    ocean_cloud_config(const OceanSurfaceFrame& surface_frame) const;

    [[nodiscard]] bool cloud_products_enabled() const noexcept;

    [[nodiscard]] bool cloud_composite_enabled() const noexcept;

    void create_cloud_resources(const cubey::vulkan::Device& device,
                                cubey::vulkan::GpuRuntime& gpu);

    void create_cloud_pipelines(const cubey::vulkan::Device& device, VkExtent2D extent,
                                std::uint32_t frame_slot_count);

    void create_cloud_environment_runtime(const cubey::vulkan::Device& device,
                                          std::uint32_t frame_slot_count);

    void create_cloud_planar_reflection(const cubey::vulkan::Device& device,
                                        VkExtent2D target_extent, std::uint32_t frame_slot_count);

    [[nodiscard]] bool cloud_environment_active() const noexcept;

    [[nodiscard]] bool cloud_planar_reflection_active() const noexcept;

    void update_cloud_shadow_fallback_descriptor(const cubey::vulkan::Device& device,
                                                 cubey::render::FrameSlot frame_slot);

    void update_cloud_planar_reflection_descriptor(const cubey::vulkan::Device& device,
                                                   cubey::render::FrameSlot frame_slot);

    void refresh_cloud_weather_if_needed(const cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu);

    void sync_cloud_runtime_after_ui(cubey::host::WindowedAppContext& context);

    void create_reference_pillar_resources(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu, VkExtent2D extent);

    void destroy_swapchain_resources();

    void sync_gpu_resources(cubey::host::WindowedAppContext& context);

    void apply_camera_preset(OceanCameraPreset preset);

    [[nodiscard]] cubey::Transform3D camera_transform() const;

    [[nodiscard]] float planet_radius_m() const;

    [[nodiscard]] OceanSurfaceFrame ocean_surface_frame() const;

    [[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
    atmosphere_environment_for_surface_frame(const OceanSurfaceFrame& surface_frame) const;

    [[nodiscard]] cubey::math::Mat4
    ocean_view_projection_matrix(VkExtent2D extent, const cubey::Transform3D& transform,
                                 float far_plane_m) const;

    [[nodiscard]] cubey::math::Mat4
    ocean_view_projection_matrix(VkExtent2D extent, const cubey::Transform3D& transform,
                                 const OceanSurfaceFrame& surface_frame) const;

    [[nodiscard]] OceanMeshDrawPlan ocean_mesh_draw_plan(VkExtent2D extent) const;

    [[nodiscard]] OceanPushConstants
    surface_push_constants(VkExtent2D extent, const OceanSurfaceFrame& surface_frame,
                           const OceanMeshPatch& patch,
                           const OceanPatchShadingPlan& shading_plan) const;

    [[nodiscard]] OceanSurfaceFeatureUniforms
    surface_feature_uniforms(const OceanSurfaceFrame& surface_frame,
                             const cubey::render::CloudLayerShadowProduct* cloud_shadow,
                             cubey::render::FrameSlot frame_slot) const;

    [[nodiscard]] OceanReferencePillarPushConstants
    reference_pillar_push_constants(VkExtent2D extent) const;

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_background_uniforms(VkExtent2D extent,
                                   const OceanSurfaceFrame& surface_frame) const;

    [[nodiscard]] cubey::CloudEnvironmentSurfaceViewInfo
    cloud_surface_view(VkExtent2D extent, const OceanSurfaceFrame& surface_frame) const;

    [[nodiscard]] cubey::CloudEnvironmentRuntimeFrame
    current_cloud_frame(VkExtent2D extent, const OceanSurfaceFrame& surface_frame);

    [[nodiscard]] cubey::render::CloudLayerShadowRequest
    cloud_shadow_request(const OceanSurfaceFrame& surface_frame) const;

    [[nodiscard]] OceanSpectrumPushConstants
    spectrum_push_constants(std::uint32_t cascade_index) const;

    [[nodiscard]] cubey::math::Vec4 atmosphere_sun_light_uniform() const;

    [[nodiscard]] OceanModulatePushConstants
    modulate_push_constants(std::uint32_t cascade_index) const;

    [[nodiscard]] OceanFftPushConstants fft_push_constants(std::uint32_t cascade_index,
                                                           std::uint32_t stage, bool horizontal,
                                                           bool first_pass) const;

    [[nodiscard]] OceanUnpackPushConstants
    unpack_push_constants(std::uint32_t cascade_index) const;

    void record_atmosphere_background(const cubey::vulkan::CommandRecorder& recorder,
                                      VkExtent2D extent,
                                      cubey::render::FrameSlot frame_slot) const;

    void record_ocean_draw(const cubey::vulkan::CommandRecorder& recorder, VkExtent2D extent,
                           cubey::render::FrameSlot frame_slot,
                           const cubey::render::CloudLayerShadowProduct* cloud_shadow) const;

    void record_reference_pillar_draw(const cubey::vulkan::CommandRecorder& recorder,
                                      VkExtent2D extent) const;

    [[nodiscard]] cubey::render::PbrPostUniforms post_uniforms() const;

    [[nodiscard]] float display_exposure() const;

    void record_ocean_post(const cubey::vulkan::CommandRecorder& recorder,
                           cubey::render::ColorTargetView target,
                           cubey::render::FrameSlot frame_slot) const;

    [[nodiscard]] OceanFrameGraph build_ocean_frame_graph(
        cubey::render::ColorTargetView color_target, cubey::render::FrameSlot frame_slot,
        OceanRenderTargetMode target_mode,
        const std::optional<cubey::CloudEnvironmentRuntimeFrame>& cloud_runtime_frame) const;

    void update_post_descriptor(const cubey::vulkan::Device& device,
                                cubey::render::FrameSlot frame_slot,
                                const cubey::render::CompiledRenderGraph& graph,
                                const cubey::render::RenderGraphResourceSet& resources,
                                cubey::render::RenderGraphTextureHandle scene_color) const;

    void record_initial_texture_transitions(const cubey::vulkan::CommandRecorder& recorder) const;

    void record_spectrum_init(const cubey::vulkan::CommandRecorder& recorder,
                              cubey::render::FrameSlot frame_slot);

    void record_modulate(const cubey::vulkan::CommandRecorder& recorder,
                         cubey::render::FrameSlot frame_slot);

    void record_fft_pass(const cubey::vulkan::CommandRecorder& recorder, std::uint32_t cascade,
                         std::uint32_t field, std::uint32_t stage, bool horizontal, bool first_pass,
                         std::uint32_t descriptor_set_index) const;

    void record_fft(const cubey::vulkan::CommandRecorder& recorder,
                    cubey::render::FrameSlot frame_slot);

    void record_unpack(const cubey::vulkan::CommandRecorder& recorder,
                       cubey::render::FrameSlot frame_slot);

    void record_ocean_compute(const cubey::vulkan::CommandRecorder& recorder,
                              cubey::render::FrameSlot frame_slot);

    void record_atmosphere_environment_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                                 cubey::render::FrameSlot frame_slot);

    void record_cloud_environment_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                            cubey::render::FrameSlot frame_slot,
                                            const cubey::CloudEnvironmentRuntimeFrame& frame);

    void
    record_cloud_planar_reflection_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                             cubey::render::FrameSlot frame_slot,
                                             const cubey::CloudEnvironmentRuntimeFrame& frame);

    void update_cloud_environment_descriptors(const cubey::vulkan::Device& device,
                                              cubey::render::FrameSlot frame_slot);

    void update_atmosphere_environment_descriptors(const cubey::vulkan::Device& device,
                                                   cubey::render::FrameSlot frame_slot);

    void record_ocean_target(VkCommandBuffer command_buffer, const cubey::vulkan::Device& device,
                             cubey::render::ColorTargetView target,
                             cubey::render::FrameSlot frame_slot,
                             OceanRenderTargetMode target_mode);

    void record_windowed_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& frame);

    OceanProjectConfig config_;
    OceanConfig ocean_config_;
    OceanSpectrumDiagnostics spectrum_diagnostics_;
    cubey::CloudEnvironmentConfig clouds_config_{};
    cubey::AtmosphereEnvironmentRunState atmosphere_state_;
    cubey::render::AtmosphereEnvironmentLighting atmosphere_lighting_;
    OceanDiagnosticsConfig diagnostics_;
    OceanRenderView render_view_ = OceanRenderView::Final;
    OceanCameraPreset camera_preset_ = OceanCameraPreset::Default;
    cubey::Camera3D camera_;
    cubey::OrbitController orbit_controller_;
    float capture_orbit_offset_radians_ = 0.0F;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<FrameStatsSnapshot> latest_frame_stats_;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    OceanGpuResources ocean_gpu_;
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::render::AtmosphereBackgroundFrame atmosphere_background_{};
    std::optional<cubey::render::Mesh> reference_pillar_mesh_;
    std::optional<cubey::render::GraphicsPipelineResource> reference_pillar_pipeline_;
    cubey::render::HdrPostFrame hdr_post_frame_;
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_background_atlases_;
    cubey::AtmosphereEnvironmentRuntime atmosphere_runtime_{};
    cubey::render::CloudPlanarReflectionRuntime cloud_planar_reflection_{};
    std::optional<cubey::render::Texture2D> cloud_shadow_fallback_texture_;
    std::optional<cubey::render::Texture2D> cloud_reflection_fallback_texture_;
    cubey::render::TerrainOceanPackedFields terrain_ocean_fields_{};
    std::optional<cubey::render::Texture2D> terrain_ocean_fields_texture_;
    std::optional<cubey::render::FrameUniformBuffer<OceanTerrainFieldUniforms>>
        terrain_ocean_field_uniforms_;
    std::optional<OceanConfig> gpu_config_;
    VkFormat pipeline_color_format_ = VK_FORMAT_UNDEFINED;
    double time_seconds_ = 0.0;
    double last_delta_seconds_ = 1.0 / 60.0;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
    double last_gpu_timing_print_seconds_ = -1.0;
    std::uint64_t windowed_frame_index_ = 0;
    std::uint64_t ocean_compute_frame_index_ = 0;
    float camera_base_yaw_ = kCameraBaseYaw;
    float camera_base_pitch_ = kCameraBasePitch;
    bool paused_ = false;
    bool reset_requested_ = false;
    bool step_requested_ = false;
    bool camera_preset_requested_ = false;
    bool textures_initialized_ = false;
    bool spectrum_initialized_ = false;
    bool foam_initialized_ = false;
};

} // namespace detail
} // namespace cubey::projects::ocean
