#include "ocean_app.h"

#include "ocean_config.h"
#include "ocean_gpu_resources.h"
#include "ocean_mesh.h"
#include "ocean_spectrum_diagnostics.h"
#include "ocean_surface_frame.h"
#include "ocean_ui.h"

#include <cubey/core/profiling.h>
#include <cubey/core/math.h>
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
#include <cubey/vulkan/vk_check.h>

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
namespace {

using cubey::FrameTiming;
using cubey::host::FrameStatsSample;
using cubey::host::FrameStatsSnapshot;

constexpr float kCameraDistance = 125.0F;
constexpr float kCameraMinDistance = 18.0F;
constexpr float kCameraMaxDistance = 8000.0F;
constexpr float kCameraBaseYaw = cubey::render::kAtmosphereEnvironmentSunriseViewYawRadians;
constexpr float kCameraBasePitch = cubey::render::kAtmosphereEnvironmentSunriseViewPitchRadians;
constexpr float kCameraNearPlane = 0.25F;
constexpr float kCameraFarPlane = 14000.0F;
constexpr float kCameraMinimumSurfaceClearanceMeters = 8.0F;
constexpr VkFormat kOceanSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kOceanDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr float kOceanSunElevationDegrees = 20.0F;
constexpr float kOceanSunAzimuthDegrees = -20.0F;
constexpr float kReferencePillarHalfWidthMeters = 0.50F;
constexpr float kReferencePillarCenterXMeters = -24.0F;
constexpr float kReferencePillarCenterZMeters = 10.0F;
constexpr float kReferencePillarAxisUX = 0.70710678F;
constexpr float kReferencePillarAxisUZ = 0.70710678F;
constexpr float kReferencePillarAxisVX = -0.70710678F;
constexpr float kReferencePillarAxisVZ = 0.70710678F;
constexpr float kReferencePillarMinYMeters = -25.0F;
constexpr float kReferencePillarMaxYMeters = 25.0F;
constexpr float kReferencePillarMeterMarkerHalfHeightMeters = 0.055F;
constexpr float kReferencePillarFiveMeterMarkerHalfHeightMeters = 0.22F;
constexpr float kReferencePillarTenMeterMarkerHalfHeightMeters = 0.34F;
constexpr float kReferencePillarMarkerHalfWidthMeters = kReferencePillarHalfWidthMeters + 0.012F;

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

static_assert(sizeof(OceanPushConstants) == sizeof(float) * 60U);
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
    OceanMeshDrawStats stats{};
};

struct OceanCameraPresetConfig {
    OceanCameraPreset preset = OceanCameraPreset::Default;
    float distance = kCameraDistance;
    float yaw = kCameraBaseYaw;
    float pitch = kCameraBasePitch;
};

[[nodiscard]] std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                                          cubey::render::FrameSlot frame_slot) {
    cubey::render::validate_frame_slot(frame_slot);
    if (frame_index >= frame_slot.count) {
        return frame_index - frame_slot.count;
    }
    return 0;
}

[[nodiscard]] float ocean_patch_mesh_cell_size(const OceanMeshPatch& patch) {
    const float span_x = patch.bounds.max_x - patch.bounds.min_x;
    const float span_z = patch.bounds.max_z - patch.bounds.min_z;
    return std::max(span_x / static_cast<float>(std::max(patch.cells_x, 1U)),
                    span_z / static_cast<float>(std::max(patch.cells_z, 1U)));
}

[[nodiscard]] float ocean_patch_snap_size(const OceanMeshPatch& patch) {
    return std::max(ocean_patch_mesh_cell_size(patch) /
                        static_cast<float>(1U << std::min(patch.level, 30U)),
                    0.001F);
}

[[nodiscard]] float ocean_patch_wave_cull_margin_m(const OceanConfig& config) {
    float scale_sum = 0.0F;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (ocean_cascade_enabled(config, cascade)) {
            scale_sum += ocean_cascade(config, cascade).displacement_scale;
        }
    }
    return std::max(96.0F, scale_sum * 24.0F * std::max(config.surface_shape_strength, 0.0F));
}

[[nodiscard]] cubey::Bounds3D ocean_mesh_patch_world_bounds(
    const OceanConfig& config,
    const OceanSurfaceFrame& surface_frame,
    const OceanMeshPatch& patch,
    cubey::math::Vec3 camera_position_m) {
    const float snap = ocean_patch_snap_size(patch);
    const float snapped_x = std::floor(camera_position_m.x / snap) * snap;
    const float snapped_z = std::floor(camera_position_m.z / snap) * snap;

    const float horizontal_margin = ocean_patch_wave_cull_margin_m(config);
    const float vertical_margin = ocean_patch_wave_cull_margin_m(config) * 1.5F;
    const float min_x = snapped_x + patch.bounds.min_x - horizontal_margin;
    const float max_x = snapped_x + patch.bounds.max_x + horizontal_margin;
    const float min_z = snapped_z + patch.bounds.min_z - horizontal_margin;
    const float max_z = snapped_z + patch.bounds.max_z + horizontal_margin;

    float min_drop = 0.0F;
    const std::array<cubey::math::Vec2, 4> corners{
        cubey::math::Vec2{min_x, min_z},
        cubey::math::Vec2{min_x, max_z},
        cubey::math::Vec2{max_x, min_z},
        cubey::math::Vec2{max_x, max_z},
    };
    for (const cubey::math::Vec2 corner : corners) {
        const float distance =
            glm::length(cubey::math::Vec2{corner.x - camera_position_m.x,
                                          corner.y - camera_position_m.z});
        min_drop = std::min(min_drop,
                            ocean_surface_curvature_drop_m(
                                distance, surface_frame.local_frame.planet_radius_m,
                                surface_frame.curvature_start_m, surface_frame.curvature_end_m,
                                surface_frame.curvature_strength));
    }

    const float water_datum = surface_frame.local_frame.water_datum_m;
    const float min_y = water_datum + min_drop - vertical_margin;
    const float max_y = water_datum + vertical_margin;
    return {
        .center = {(min_x + max_x) * 0.5F, (min_y + max_y) * 0.5F,
                   (min_z + max_z) * 0.5F},
        .half_extent = {(max_x - min_x) * 0.5F, (max_y - min_y) * 0.5F,
                        (max_z - min_z) * 0.5F},
    };
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

[[nodiscard]] std::string ocean_gpu_timing_label(const char* phase, std::uint32_t cascade) {
    return std::string("ocean.") + phase + ".c" + std::to_string(cascade);
}

[[nodiscard]] cubey::render::PrimitiveVec3 linear_srgb(
    cubey::render::PrimitiveVec3 srgb_color) {
    return cubey::render::srgb_to_linear_rgb(srgb_color);
}

[[nodiscard]] cubey::render::PrimitiveVec3 pillar_position(float local_u, float y,
                                                           float local_v) {
    return {
        kReferencePillarCenterXMeters + local_u * kReferencePillarAxisUX +
            local_v * kReferencePillarAxisVX,
        y,
        kReferencePillarCenterZMeters + local_u * kReferencePillarAxisUZ +
            local_v * kReferencePillarAxisVZ,
    };
}

[[nodiscard]] cubey::render::PrimitiveVec3 pillar_u_normal(float sign) {
    return {sign * kReferencePillarAxisUX, 0.0F, sign * kReferencePillarAxisUZ};
}

[[nodiscard]] cubey::render::PrimitiveVec3 pillar_v_normal(float sign) {
    return {sign * kReferencePillarAxisVX, 0.0F, sign * kReferencePillarAxisVZ};
}

void append_pillar_quad(
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>& mesh,
    cubey::render::PrimitiveVec3 p0, cubey::render::PrimitiveVec3 p1,
    cubey::render::PrimitiveVec3 p2, cubey::render::PrimitiveVec3 p3,
    cubey::render::PrimitiveVec3 normal, cubey::render::PrimitiveVec3 color) {
    if (mesh.vertices.size() > std::numeric_limits<std::uint16_t>::max() - 4U) {
        throw std::runtime_error("ocean reference pillar mesh exceeds uint16 index range");
    }
    const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({.position = p0, .color = color, .normal = normal});
    mesh.vertices.push_back({.position = p1, .color = color, .normal = normal});
    mesh.vertices.push_back({.position = p2, .color = color, .normal = normal});
    mesh.vertices.push_back({.position = p3, .color = color, .normal = normal});
    mesh.indices.insert(mesh.indices.end(),
                        {base, static_cast<std::uint16_t>(base + 1U),
                         static_cast<std::uint16_t>(base + 2U), base,
                         static_cast<std::uint16_t>(base + 2U),
                         static_cast<std::uint16_t>(base + 3U)});
}

void append_pillar_band(
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>& mesh,
    float half_width, float min_y, float max_y, cubey::render::PrimitiveVec3 color,
    bool bottom_cap, bool top_cap) {
    append_pillar_quad(mesh, pillar_position(-half_width, min_y, half_width),
                       pillar_position(half_width, min_y, half_width),
                       pillar_position(half_width, max_y, half_width),
                       pillar_position(-half_width, max_y, half_width), pillar_v_normal(1.0F),
                       color);
    append_pillar_quad(mesh, pillar_position(half_width, min_y, -half_width),
                       pillar_position(-half_width, min_y, -half_width),
                       pillar_position(-half_width, max_y, -half_width),
                       pillar_position(half_width, max_y, -half_width), pillar_v_normal(-1.0F),
                       color);
    append_pillar_quad(mesh, pillar_position(-half_width, min_y, -half_width),
                       pillar_position(-half_width, min_y, half_width),
                       pillar_position(-half_width, max_y, half_width),
                       pillar_position(-half_width, max_y, -half_width), pillar_u_normal(-1.0F),
                       color);
    append_pillar_quad(mesh, pillar_position(half_width, min_y, half_width),
                       pillar_position(half_width, min_y, -half_width),
                       pillar_position(half_width, max_y, -half_width),
                       pillar_position(half_width, max_y, half_width), pillar_u_normal(1.0F),
                       color);
    if (bottom_cap) {
        append_pillar_quad(mesh, pillar_position(-half_width, min_y, -half_width),
                           pillar_position(half_width, min_y, -half_width),
                           pillar_position(half_width, min_y, half_width),
                           pillar_position(-half_width, min_y, half_width),
                           {0.0F, -1.0F, 0.0F}, color);
    }
    if (top_cap) {
        append_pillar_quad(mesh, pillar_position(-half_width, max_y, half_width),
                           pillar_position(half_width, max_y, half_width),
                           pillar_position(half_width, max_y, -half_width),
                           pillar_position(-half_width, max_y, -half_width),
                           {0.0F, 1.0F, 0.0F}, color);
    }
}

[[nodiscard]] cubey::render::PrimitiveVec3 reference_pillar_marker_color(int meter) {
    if (meter % 10 == 0) {
        return linear_srgb({1.0F, 0.08F, 0.05F});
    }
    if (meter % 5 == 0) {
        return linear_srgb({1.0F, 0.78F, 0.10F});
    }
    return linear_srgb({0.015F, 0.015F, 0.015F});
}

[[nodiscard]] float reference_pillar_marker_half_height(int meter) {
    if (meter % 10 == 0) {
        return kReferencePillarTenMeterMarkerHalfHeightMeters;
    }
    if (meter % 5 == 0) {
        return kReferencePillarFiveMeterMarkerHalfHeightMeters;
    }
    return kReferencePillarMeterMarkerHalfHeightMeters;
}

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>
make_ocean_reference_pillar_mesh() {
    constexpr int min_marker_meter = -25;
    constexpr int max_marker_meter = 25;
    constexpr std::uint32_t marker_count =
        static_cast<std::uint32_t>(max_marker_meter - min_marker_meter + 1);
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal> mesh;
    mesh.vertices.reserve(24U + marker_count * 16U);
    mesh.indices.reserve(36U + marker_count * 24U);

    const cubey::render::PrimitiveVec3 body = linear_srgb({0.92F, 0.92F, 0.88F});
    append_pillar_band(mesh, kReferencePillarHalfWidthMeters, kReferencePillarMinYMeters,
                       kReferencePillarMaxYMeters, body, true, true);

    for (int marker = min_marker_meter; marker <= max_marker_meter; ++marker) {
        const float center_y = static_cast<float>(marker);
        const float half_height = reference_pillar_marker_half_height(marker);
        const float min_y =
            std::max(kReferencePillarMinYMeters, center_y - half_height);
        const float max_y =
            std::min(kReferencePillarMaxYMeters, center_y + half_height);
        append_pillar_band(mesh, kReferencePillarMarkerHalfWidthMeters, min_y, max_y,
                           reference_pillar_marker_color(marker), false, false);
    }
    return mesh;
}

[[nodiscard]] cubey::render::MaterialPassInfo ocean_reference_pillar_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(OceanReferencePillarPushConstants),
    };
    return {
        .label = "ocean.reference_pillar",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
}

[[nodiscard]] float radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

[[nodiscard]] cubey::AtmosphereEnvironmentRunState
ocean_atmosphere_run_state(const RunConfig& run_config) {
    return cubey::atmosphere_environment_run_state_from_config(
        run_config.atmosphere,
        {
            .sun_elevation_degrees = kOceanSunElevationDegrees,
            .sun_azimuth_degrees = kOceanSunAzimuthDegrees,
            .ground_mode =
                cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
            .reference_geometry_enabled = false,
        });
}

[[nodiscard]] cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files() {
    return cubey::render::cloud_layer_runtime_shader_files(
        std::filesystem::path(CUBEY_OCEAN_SHADER_DIR),
        cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
}

[[nodiscard]] std::uint32_t log2_exact(std::uint32_t value) {
    if (!ocean_is_power_of_two(value)) {
        throw std::runtime_error("ocean FFT resolution must be a power of two");
    }
    std::uint32_t result = 0;
    while (value > 1U) {
        value >>= 1U;
        ++result;
    }
    return result;
}

[[nodiscard]] cubey::render::ComputeDispatchGroups
ocean_dispatch_groups(const OceanConfig& config, std::uint32_t cascade) {
    const std::uint32_t map_size = ocean_cascade_map_size(config, cascade);
    return cubey::render::ceil_dispatch_groups(map_size, map_size, 16U);
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc
ocean_depth_texture_desc(const char* label, VkExtent2D extent, VkFormat format) {
    return {
        .label = label,
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

[[nodiscard]] OceanCameraPresetConfig ocean_camera_preset_config(OceanCameraPreset preset) {
    switch (preset) {
    case OceanCameraPreset::Default:
        return {.preset = preset,
                .distance = kCameraDistance,
                .yaw = kCameraBaseYaw,
                .pitch = kCameraBasePitch};
    case OceanCameraPreset::Low:
        return {.preset = preset, .distance = 180.0F, .yaw = 0.42F, .pitch = -0.08F};
    case OceanCameraPreset::Mid:
        return {.preset = preset, .distance = 360.0F, .yaw = 0.32F, .pitch = -0.28F};
    case OceanCameraPreset::High:
        return {.preset = preset, .distance = 650.0F, .yaw = 0.24F, .pitch = -0.46F};
    case OceanCameraPreset::Close:
        return {.preset = preset, .distance = 48.0F, .yaw = 0.62F, .pitch = -0.28F};
    case OceanCameraPreset::Overhead:
        return {.preset = preset, .distance = 220.0F, .yaw = 0.20F, .pitch = -1.05F};
    case OceanCameraPreset::Wide:
        return {.preset = preset, .distance = 900.0F, .yaw = 0.20F, .pitch = -0.70F};
    }
    return {.preset = OceanCameraPreset::Default,
            .distance = kCameraDistance,
            .yaw = kCameraBaseYaw,
            .pitch = kCameraBasePitch};
}

[[nodiscard]] OceanCameraPreset ocean_camera_preset_from_name(std::string_view name) {
    if (name.empty() || name == "default") {
        return OceanCameraPreset::Default;
    }
    if (name == "low") {
        return OceanCameraPreset::Low;
    }
    if (name == "mid") {
        return OceanCameraPreset::Mid;
    }
    if (name == "high") {
        return OceanCameraPreset::High;
    }
    if (name == "close") {
        return OceanCameraPreset::Close;
    }
    if (name == "overhead") {
        return OceanCameraPreset::Overhead;
    }
    if (name == "wide") {
        return OceanCameraPreset::Wide;
    }
    throw std::runtime_error("unknown ocean camera preset: " + std::string(name));
}

[[nodiscard]] bool ocean_resource_layout_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.map_size != rhs.map_size || lhs.field_precision != rhs.field_precision ||
           lhs.cascade_enabled != rhs.cascade_enabled ||
           lhs.cascade_map_sizes != rhs.cascade_map_sizes;
}

[[nodiscard]] bool ocean_terrain_field_source_changed(const OceanConfig& lhs,
                                                      const OceanConfig& rhs) {
    return lhs.mesh_extent != rhs.mesh_extent || lhs.depth != rhs.depth;
}

[[nodiscard]] bool ocean_wave_source_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.depth != rhs.depth || lhs.spectral_domains_enabled != rhs.spectral_domains_enabled ||
           lhs.cascades != rhs.cascades || lhs.cascade_enabled != rhs.cascade_enabled;
}

[[nodiscard]] bool ocean_cloud_environment_resource_changed(const OceanConfig& lhs,
                                                            const OceanConfig& rhs) {
    return lhs.cloud_environment_extent != rhs.cloud_environment_extent ||
           lhs.cloud_environment_update_hz != rhs.cloud_environment_update_hz;
}

[[nodiscard]] bool ocean_cloud_planar_resource_changed(const OceanConfig& lhs,
                                                       const OceanConfig& rhs) {
    return lhs.cloud_planar_resolution_scale != rhs.cloud_planar_resolution_scale ||
           lhs.cloud_planar_view_steps != rhs.cloud_planar_view_steps ||
           lhs.cloud_planar_guard_band != rhs.cloud_planar_guard_band;
}

[[nodiscard]] std::uint32_t ocean_enabled_cascade_mask(const OceanConfig& config) {
    std::uint32_t mask = 0U;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (ocean_cascade_enabled(config, cascade)) {
            mask |= 1U << cascade;
        }
    }
    return mask;
}

[[nodiscard]] bool ocean_should_update_cascade(const OceanConfig& config, std::uint32_t cascade,
                                               std::uint64_t frame_index, bool first_update) {
    if (!ocean_cascade_enabled(config, cascade)) {
        return false;
    }
    if (first_update) {
        return true;
    }
    const std::uint32_t interval = ocean_cascade_update_interval(config, cascade);
    return interval <= 1U || (frame_index % interval) == 0U;
}

[[nodiscard]] OceanTerrainFieldUniforms
ocean_terrain_field_uniforms(const cubey::render::TerrainOceanPackedFields& fields, bool enabled) {
    const cubey::render::TerrainOceanGridDesc& desc = fields.desc;
    const float span_x =
        std::max(static_cast<float>(desc.width > 1U ? desc.width - 1U : 1U) * desc.cell_size_m,
                 desc.cell_size_m);
    const float span_z =
        std::max(static_cast<float>(desc.height > 1U ? desc.height - 1U : 1U) * desc.cell_size_m,
                 desc.cell_size_m);
    return {
        .uv_transform =
            {
                desc.origin_x_m - span_x * 0.5F,
                desc.origin_z_m - span_z * 0.5F,
                1.0F / span_x,
                1.0F / span_z,
            },
        .ranges_flags =
            {
                fields.max_water_depth_m,
                fields.max_abs_shore_sdf_m,
                fields.max_slope,
                enabled ? 1.0F : 0.0F,
            },
    };
}

[[nodiscard]] cubey::render::Texture2D create_ocean_cloud_shadow_fallback_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
    const std::array<std::uint8_t, 4> white_pixel{255U, 255U, 255U, 255U};
    return cubey::render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {1U, 1U},
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .rgba8 = std::span<const std::uint8_t>{white_pixel.data(), white_pixel.size()},
            .create_sampler = true,
            .sampler =
                {
                    .min_filter = VK_FILTER_LINEAR,
                    .mag_filter = VK_FILTER_LINEAR,
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                    .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                },
        });
}

[[nodiscard]] cubey::render::Texture2D create_ocean_cloud_reflection_fallback_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
    const std::array<std::uint8_t, 4> clear_cloud_pixel{0U, 0U, 0U, 255U};
    return cubey::render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {1U, 1U},
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .rgba8 =
                std::span<const std::uint8_t>{clear_cloud_pixel.data(), clear_cloud_pixel.size()},
            .create_sampler = true,
            .sampler =
                {
                    .min_filter = VK_FILTER_LINEAR,
                    .mag_filter = VK_FILTER_LINEAR,
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                },
        });
}

[[nodiscard]] cubey::render::TerrainOceanPackedFields
make_ocean_diagnostic_terrain_fields(const OceanConfig& config, float water_datum_m) {
    constexpr std::uint32_t field_extent = 129U;
    const float cell_size = (config.mesh_extent * 2.0F) / static_cast<float>(field_extent - 1U);
    cubey::render::TerrainOceanFieldView field_view;
    field_view.desc = {
        .version = 1,
        .seed = 0x4f6365616eULL,
        .width = field_extent,
        .height = field_extent,
        .cell_size_m = cell_size,
        .sea_level_m = water_datum_m,
        .origin_x_m = 0.0F,
        .origin_z_m = 0.0F,
    };

    const std::size_t count = cubey::render::terrain_ocean_sample_count(field_view.desc);
    std::vector<float> height(count, 0.0F);
    std::vector<float> water_depth(count, 0.0F);
    std::vector<float> shore_sdf(count, 0.0F);
    std::vector<float> slope(count, 0.0F);

    const float island_radius = config.mesh_extent * 0.22F;
    const float shoal_radius = config.mesh_extent * 0.42F;
    const float minimum_depth = std::max(config.depth * 0.10F, 0.75F);
    const float deep_depth = std::max(config.depth, minimum_depth + 1.0F);
    for (std::uint32_t y = 0; y < field_view.desc.height; ++y) {
        const float z =
            (static_cast<float>(y) - static_cast<float>(field_view.desc.height - 1U) * 0.5F) *
            cell_size;
        for (std::uint32_t x = 0; x < field_view.desc.width; ++x) {
            const float world_x =
                (static_cast<float>(x) - static_cast<float>(field_view.desc.width - 1U) * 0.5F) *
                cell_size;
            const std::size_t index =
                static_cast<std::size_t>(y) * field_view.desc.width + static_cast<std::size_t>(x);
            const float radius = std::sqrt(world_x * world_x + z * z);
            const float signed_shore = island_radius - radius;
            const float shoal = 1.0F - std::clamp((radius - island_radius) /
                                                      std::max(shoal_radius - island_radius, 1.0F),
                                                  0.0F, 1.0F);
            const float depth = std::max(minimum_depth, deep_depth * (1.0F - shoal * 0.82F));

            shore_sdf[index] = signed_shore;
            water_depth[index] = signed_shore > 0.0F ? 0.0F : depth;
            height[index] = signed_shore > 0.0F ? signed_shore * 0.035F : -depth;
            slope[index] = std::clamp(shoal * 0.75F + std::abs(signed_shore) /
                                                          std::max(config.mesh_extent, 1.0F),
                                      0.0F, 1.0F);
        }
    }

    field_view.height_m = std::span<const float>(height.data(), height.size());
    field_view.water_depth_m = std::span<const float>(water_depth.data(), water_depth.size());
    field_view.shore_sdf_m = std::span<const float>(shore_sdf.data(), shore_sdf.size());
    field_view.slope = std::span<const float>(slope.data(), slope.size());
    return cubey::render::pack_terrain_ocean_fields(field_view);
}

class OceanApp {
  public:
    explicit OceanApp(RunConfig config)
        : config_(std::move(config)), ocean_config_(ocean_config_from_run_config(config_)),
          spectrum_diagnostics_(ocean_spectrum_diagnostics(ocean_config_)),
          clouds_config_(ocean_cloud_config_from_run_config(config_)),
          atmosphere_state_(ocean_atmosphere_run_state(config_)),
          atmosphere_lighting_(
              cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment)),
          render_view_(ocean_config_.render_view) {
        diagnostics_.selected_cascade = config_.ocean.cascade;
        diagnostics_.wire_overlay = config_.ocean.wire_overlay;
        if (cubey::run_config_float_is_set(config_.ocean.wire_opacity)) {
            diagnostics_.wire_opacity = config_.ocean.wire_opacity;
        }
        camera_preset_ = ocean_camera_preset_from_name(config_.ocean.camera_preset);
        camera_.set_projection(camera_.fovy_radians(), kCameraNearPlane, kCameraFarPlane);
        orbit_controller_.set_home_distance(kCameraDistance);
        orbit_controller_.set_distance_limits(kCameraMinDistance, kCameraMaxDistance);
        orbit_controller_.set_auto_rotation_speed(0.0F);
        apply_camera_preset(camera_preset_);
    }

    OceanApp(const OceanApp&) = delete;
    OceanApp& operator=(const OceanApp&) = delete;

    ~OceanApp() {
        destroy_swapchain_resources();
    }

    int run() {
        if (config_.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_windowed(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) {
            bool atmosphere_changed = false;
            const cubey::render::CloudEnvironmentProbeSnapshot cloud_environment =
                cloud_environment_runtime_.snapshot();
            const OceanMeshDrawPlan draw_plan =
                ocean_mesh_draw_plan(context.swapchain().extent());
            draw_ocean_ui({
                .config = ocean_config_,
                .diagnostics = diagnostics_,
                .spectrum_diagnostics = spectrum_diagnostics_,
                .surface_frame = draw_plan.surface_frame,
                .draw_stats = draw_plan.stats,
                .atmosphere = atmosphere_state_,
                .clouds = clouds_config_,
                .performance =
                    {
                        .frame_stats = latest_frame_stats_,
                        .latest_fps = latest_fps_,
                        .latest_frame_ms = latest_frame_ms_,
                        .process = process_stats_.sample(),
                        .device_memory_budget = context.device().device_memory_budget(),
                        .gpu_timings = ocean_gpu_.latest_timings(),
                    },
                .cloud_environment_generation = cloud_environment.generation,
                .cloud_environment_blend = cloud_environment.blend,
                .cloud_environment_age_seconds = cloud_environment_runtime_.age_seconds(),
                .cloud_environment_valid = cloud_environment.valid,
                .render_view = render_view_,
                .camera_preset = camera_preset_,
                .paused = paused_,
                .reset_requested = reset_requested_,
                .step_requested = step_requested_,
                .camera_preset_requested = camera_preset_requested_,
                .atmosphere_changed = atmosphere_changed,
            });
            if (camera_preset_requested_) {
                apply_camera_preset(camera_preset_);
                camera_preset_requested_ = false;
            }
            if (atmosphere_changed) {
                refresh_atmosphere_lighting();
            }
        };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_windowed_frame(context, frame);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = "ocean",
                .ready_status = "rendering ocean project",
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            const cubey::host::HeadlessRenderTarget& target = context.render_target();
            create_pipeline(context.device(), context.gpu(), target.format, target.extent,
                            cubey::host::headless_capture_frame_slot_count(config_));
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            time_seconds_ = frame.timing.elapsed_seconds;
            last_delta_seconds_ =
                frame.timing.delta_seconds > 0.0 ? frame.timing.delta_seconds : (1.0 / 60.0);
            update_atmosphere_time(frame.timing.delta_seconds);
            if (cloud_environment_active()) {
                cloud_environment_runtime_.advance(frame.timing.delta_seconds);
            }
            collect_gpu_timings(context.profile_recorder(), frame.index, frame.frame_slot,
                                frame.timing.elapsed_seconds);
            record_ocean_target(command_buffer, context.device(), target, frame.frame_slot,
                                OceanRenderTargetMode::ColorAttachment);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) {
            destroy_swapchain_resources();
        };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void update_windowed(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        const auto input = context.filtered_input();
        if (input.key_pressed(cubey::input::Key::Space)) {
            paused_ = !paused_;
        }
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_requested_ = true;
        }
        if (input.key_pressed(cubey::input::Key::D)) {
            render_view_ = next_ocean_render_view(render_view_);
        }

        orbit_controller_.update_pointer_input(input, timing.delta_seconds);
        if (reset_requested_) {
            time_seconds_ = 0.0;
            foam_initialized_ = false;
            apply_camera_preset(camera_preset_);
            reset_requested_ = false;
        }
        if (paused_ && step_requested_) {
            time_seconds_ += 1.0 / 60.0;
            step_requested_ = false;
        } else if (!paused_) {
            time_seconds_ += timing.delta_seconds;
            step_requested_ = false;
        }
        last_delta_seconds_ = timing.delta_seconds > 0.0 ? timing.delta_seconds : (1.0 / 60.0);
        update_atmosphere_time(timing.delta_seconds);
        if (cloud_environment_active()) {
            cloud_environment_runtime_.advance(timing.delta_seconds);
        }
        sync_gpu_resources(context);
    }

    void refresh_atmosphere_lighting() {
        atmosphere_lighting_ =
            cubey::render::atmosphere_environment_lighting(atmosphere_state_.environment);
        if (atmosphere_runtime_.resources_created()) {
            atmosphere_runtime_.set_environment(
                atmosphere_state_.environment,
                cubey::AtmosphereReflectionProbeUpdateMode::CoherentFull);
        }
    }

    void update_atmosphere_time(double delta_seconds) {
        if (cubey::atmosphere_environment_advance_time(atmosphere_state_, delta_seconds)) {
            refresh_atmosphere_lighting();
        }
    }

    std::optional<FrameStatsSample> record_frame_stats(VkExtent2D extent,
                                                       const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const OceanMeshDrawPlan draw_plan = ocean_mesh_draw_plan(extent);
        const FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = draw_plan.stats.submitted_triangles,
        };
        if (std::optional<FrameStatsSnapshot> stats = ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void collect_gpu_timings(cubey::profiling::ProfileRecorder* profile_recorder,
                             std::uint64_t frame_index,
                             cubey::render::FrameSlot frame_slot,
                             double elapsed_seconds) {
        cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
        if (profiler == nullptr) {
            return;
        }
        profiler->collect(frame_slot.index);
        record_gpu_timings(profile_recorder, collected_profile_frame_index(frame_index, frame_slot),
                           ocean_gpu_.latest_timings());
        maybe_print_gpu_timings(elapsed_seconds);
    }

    void maybe_print_gpu_timings(double elapsed_seconds) {
        if (!config_.print_frame_stats) {
            return;
        }
        const std::vector<cubey::vulkan::GpuPassTiming>& timings = ocean_gpu_.latest_timings();
        if (timings.empty()) {
            return;
        }
        if (last_gpu_timing_print_seconds_ >= 0.0 &&
            elapsed_seconds - last_gpu_timing_print_seconds_ < 1.0) {
            return;
        }
        last_gpu_timing_print_seconds_ = elapsed_seconds;
        std::printf("ocean_gpu:");
        for (const cubey::vulkan::GpuPassTiming& timing : timings) {
            std::printf(" %s=%.3fms", timing.label.c_str(), timing.milliseconds);
        }
        std::printf("\n");
    }

    void create_pipeline(cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                         VkFormat color_format, VkExtent2D extent,
                         std::uint32_t frame_slot_count = 1U) {
        create_atmosphere_environment_runtime(device, gpu, frame_slot_count);
        ocean_gpu_.create(device, OceanGpuResourceConfig{
                                      .ocean = ocean_config_,
                                      .shader_dir = CUBEY_OCEAN_SHADER_DIR,
                                      .color_format = kOceanSceneColorFormat,
                                      .depth_format = kOceanDepthFormat,
                                      .target_extent = extent,
                                      .frame_slot_count = frame_slot_count,
                                  });
        cloud_shadow_fallback_texture_.emplace(
            create_ocean_cloud_shadow_fallback_texture(device, gpu));
        cloud_reflection_fallback_texture_.emplace(
            create_ocean_cloud_reflection_fallback_texture(device, gpu));
        for (std::uint32_t index = 0; index < frame_slot_count; ++index) {
            const cubey::render::FrameSlot frame_slot{.index = index, .count = frame_slot_count};
            update_cloud_shadow_fallback_descriptor(device, frame_slot);
            update_cloud_planar_reflection_descriptor(device, frame_slot);
        }
        create_terrain_ocean_field_resources(device, gpu, frame_slot_count);
        ocean_gpu_.update_terrain_ocean_field_descriptor(device,
                                                         terrain_ocean_fields_texture_.value());
        update_terrain_ocean_field_uniform_descriptors(device);
        const cubey::render::AtmosphereReflectionProbe& atmosphere_probe =
            atmosphere_runtime_.reflection_probe();
        ocean_gpu_.update_atmosphere_probe_descriptors(device, atmosphere_probe.prefiltered_cube(),
                                                       atmosphere_probe.sky_radiance_cube());
        create_atmosphere_background_frame(device, extent, frame_slot_count);
        if (clouds_config_.enabled) {
            create_cloud_resources(device, gpu);
            create_cloud_pipelines(device, extent, frame_slot_count);
            create_cloud_environment_runtime(device, frame_slot_count);
            create_cloud_planar_reflection(device, extent, frame_slot_count);
        }
        create_reference_pillar_resources(device, gpu, extent);
        hdr_post_frame_.create_materials(device, {
                                                     .frame_slot_count = frame_slot_count,
                                                 });
        const std::array<cubey::render::ShaderStageFile, 2> post_shader_stage_files{
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .path = std::filesystem::path(CUBEY_OCEAN_SHADER_DIR) / "forward_pbr_post.vert.spv",
            },
            cubey::render::ShaderStageFile{
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .path = std::filesystem::path(CUBEY_OCEAN_SHADER_DIR) / "forward_pbr_post.frag.spv",
            },
        };
        hdr_post_frame_.create_pipeline(device, {
                                                    .extent = extent,
                                                    .color_format = color_format,
                                                    .shader_stage_files = post_shader_stage_files,
                                                });
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
        pipeline_color_format_ = color_format;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        ocean_compute_frame_index_ = 0;
        gpu_config_ = ocean_config_;
    }

    void create_terrain_ocean_field_resources(cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu,
                                              std::uint32_t frame_slot_count) {
        terrain_ocean_fields_ = make_ocean_diagnostic_terrain_fields(
            ocean_config_, ocean_surface_frame().local_frame.water_datum_m);
        terrain_ocean_fields_texture_.emplace(
            cubey::render::create_uploaded_terrain_ocean_field_texture(device, gpu,
                                                                       terrain_ocean_fields_));
        terrain_ocean_field_uniforms_.emplace(device, frame_slot_count);
    }

    void upload_terrain_ocean_field_uniform(cubey::render::FrameSlot frame_slot) const {
        if (!terrain_ocean_field_uniforms_.has_value()) {
            throw std::runtime_error("ocean terrain field uniforms are not initialized");
        }
        terrain_ocean_field_uniforms_->upload(
            frame_slot, ocean_terrain_field_uniforms(terrain_ocean_fields_,
                                                     ocean_config_.terrain_fields_enabled));
    }

    void update_terrain_ocean_field_uniform_descriptors(const cubey::vulkan::Device& device) {
        if (!terrain_ocean_field_uniforms_.has_value()) {
            throw std::runtime_error("ocean terrain field uniforms are not initialized");
        }
        const std::uint32_t slot_count = terrain_ocean_field_uniforms_->slot_count();
        for (std::uint32_t index = 0; index < slot_count; ++index) {
            const cubey::render::FrameSlot frame_slot{.index = index, .count = slot_count};
            upload_terrain_ocean_field_uniform(frame_slot);
            ocean_gpu_.update_terrain_ocean_field_uniform_descriptor(
                device, frame_slot, terrain_ocean_field_uniforms_->buffer(frame_slot).handle(),
                terrain_ocean_field_uniforms_->range());
        }
    }

    void create_atmosphere_environment_runtime(cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu,
                                               std::uint32_t frame_slot_count) {
        if (!atmosphere_background_atlases_.has_value()) {
            atmosphere_background_atlases_.emplace(
                cubey::render::create_atmosphere_background_generated_textures(
                    device, gpu,
                    {
                        .night_sky_extent = 128,
                    }));
        }
        if (!atmosphere_runtime_.resources_created()) {
            atmosphere_runtime_.create_resources(
                device, cubey::AtmosphereEnvironmentRuntimeResourceConfig{
                            .reflection_extent = 64,
                            .reflection_mip_levels = 5,
                            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                            .frame_slot_count = frame_slot_count,
                            .atmosphere_textures = atmosphere_background_atlases_->bindings(),
                        });
            const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
            atmosphere_runtime_.create_pipelines(
                device,
                cubey::AtmosphereEnvironmentRuntimePipelineConfig{
                    .atmosphere_vertex_shader = shader_dir / "atmosphere.vert.spv",
                    .atmosphere_fragment_shader = shader_dir / "atmosphere.frag.spv",
                    .reflection_prefilter_vertex_shader = shader_dir / "atmosphere.vert.spv",
                    .reflection_prefilter_fragment_shader =
                        shader_dir / "atmosphere_reflection_prefilter.frag.spv",
                });
            atmosphere_runtime_.mark_full_update_pending();
        }
        atmosphere_runtime_.set_environment(atmosphere_state_.environment);
    }

    void create_atmosphere_background_frame(const cubey::vulkan::Device& device, VkExtent2D extent,
                                            std::uint32_t frame_slot_count) {
        if (!atmosphere_background_atlases_.has_value()) {
            throw std::runtime_error("ocean atmosphere background textures are not initialized");
        }
        atmosphere_background_.destroy();
        atmosphere_background_.create_materials(
            device, cubey::render::AtmosphereBackgroundFrameMaterialConfig{
                        .frame_slot_count = frame_slot_count,
                        .textures = atmosphere_background_atlases_->bindings(),
                    });
        const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_dir / "atmosphere.vert.spv"),
            cubey::render::fragment_shader_file(shader_dir / "atmosphere.frag.spv"),
        };
        atmosphere_background_.create_pipeline(
            device, cubey::render::AtmosphereBackgroundFramePipelineConfig{
                        .extent = extent,
                        .color_format = kOceanSceneColorFormat,
                        .depth_format = kOceanDepthFormat,
                        .shader_stage_files = shaders,
                    });
    }

    [[nodiscard]] cubey::render::CloudLayerConfig
    ocean_cloud_config(float elapsed_seconds, const OceanSurfaceFrame& surface_frame) const {
        cubey::render::CloudLayerConfig config = clouds_config_.layer;
        config.planet_radius_m = surface_frame.local_frame.planet_radius_m;
        config.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
        config.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
        config.orbit_representation = cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
        config.wind_offset_m = elapsed_seconds * clouds_config_.wind_speed_mps;
        return config;
    }

    [[nodiscard]] bool cloud_products_enabled() const noexcept {
        return clouds_config_.enabled && cloud_global_resources_created_;
    }

    [[nodiscard]] bool cloud_composite_enabled() const noexcept {
        return render_view_ == OceanRenderView::Final && cloud_products_enabled();
    }

    void create_cloud_resources(const cubey::vulkan::Device& device,
                                cubey::vulkan::GpuRuntime& gpu) {
        if (cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.create_generated_resources(
            device, gpu, cloud_runtime_shader_files().generated,
            ocean_cloud_config(static_cast<float>(time_seconds_), ocean_surface_frame()));
        cloud_global_resources_created_ = true;
    }

    void create_cloud_pipelines(const cubey::vulkan::Device& device, VkExtent2D extent,
                                std::uint32_t frame_slot_count) {
        if (!cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.create_swapchain_resources(
            device, cloud_runtime_shader_files(),
            cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
            kOceanSceneColorFormat, extent, frame_slot_count,
            ocean_cloud_config(static_cast<float>(time_seconds_), ocean_surface_frame()));
    }

    void create_cloud_environment_runtime(const cubey::vulkan::Device& device,
                                          std::uint32_t frame_slot_count) {
        cloud_environment_runtime_.destroy();
        if (!cloud_global_resources_created_) {
            return;
        }
        const cubey::render::AtmosphereReflectionProbe& atmosphere_probe =
            atmosphere_runtime_.reflection_probe();
        cloud_environment_runtime_.create_resources(
            device,
            cubey::render::CloudEnvironmentProbeConfig{
                .extent = ocean_config_.cloud_environment_extent,
                .mip_levels = 5U,
                .view_steps = 32U,
                .update_hz = ocean_config_.cloud_environment_update_hz,
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .frame_slot_count = frame_slot_count,
            },
            cloud_runtime_.generated_resources(), atmosphere_probe.sky_radiance_cube());
        const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
        cloud_environment_runtime_.create_pipelines(
            device,
            cubey::render::CloudEnvironmentProbePipelineConfig{
                .cloud_march = cubey::render::compute_shader_file(
                    shader_dir / "surface_cloud_march.comp.spv"),
                .prefilter_vertex =
                    cubey::render::vertex_shader_file(shader_dir / "atmosphere.vert.spv"),
                .prefilter_fragment = cubey::render::fragment_shader_file(
                    shader_dir / "cloud_environment_prefilter.frag.spv"),
            });
    }

    void create_cloud_planar_reflection(const cubey::vulkan::Device& device,
                                        VkExtent2D target_extent,
                                        std::uint32_t frame_slot_count) {
        cloud_planar_reflection_.destroy();
        if (!cloud_global_resources_created_) {
            return;
        }
        cloud_planar_reflection_.create_resources(
            device,
            cubey::render::CloudPlanarReflectionConfig{
                .target_extent = target_extent,
                .resolution_scale = ocean_config_.cloud_planar_resolution_scale,
                .mip_levels = 6U,
                .view_steps = ocean_config_.cloud_planar_view_steps,
                .guard_band = ocean_config_.cloud_planar_guard_band,
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .frame_slot_count = frame_slot_count,
            },
            cloud_runtime_.generated_resources());
        const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
        cloud_planar_reflection_.create_pipelines(
            device,
            cubey::render::CloudPlanarReflectionPipelineConfig{
                .cloud_march = cubey::render::compute_shader_file(
                    shader_dir / "surface_cloud_march.comp.spv"),
                .filter_vertex =
                    cubey::render::vertex_shader_file(shader_dir / "cloud.vert.spv"),
                .filter_fragment = cubey::render::fragment_shader_file(
                    shader_dir / "cloud_planar_filter.frag.spv"),
            });
    }

    [[nodiscard]] bool cloud_environment_active() const noexcept {
        return clouds_config_.enabled && cloud_global_resources_created_ &&
               ocean_config_.cloud_reflection_strength > 0.0F;
    }

    [[nodiscard]] bool cloud_planar_reflection_active() const noexcept {
        return clouds_config_.enabled && cloud_global_resources_created_ &&
               ocean_config_.cloud_reflection_strength > 0.0F &&
               ocean_config_.cloud_reflection_source == OceanCloudReflectionSource::Planar;
    }

    void update_cloud_shadow_fallback_descriptor(
        const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot) {
        if (!cloud_shadow_fallback_texture_.has_value()) {
            throw std::runtime_error("ocean cloud shadow fallback is not initialized");
        }
        const cubey::render::Texture2D& fallback = cloud_shadow_fallback_texture_.value();
        ocean_gpu_.update_cloud_shadow_descriptor(
            device, frame_slot, fallback.sampler().handle(), fallback.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void update_cloud_planar_reflection_descriptor(
        const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot) {
        if (cloud_planar_reflection_.resources_created()) {
            const cubey::render::CloudPlanarReflectionSnapshot planar =
                cloud_planar_reflection_.snapshot(frame_slot);
            if (planar.valid && planar.texture != nullptr) {
                ocean_gpu_.update_cloud_planar_reflection_descriptor(device, frame_slot,
                                                                      *planar.texture);
                return;
            }
        }
        if (!cloud_reflection_fallback_texture_.has_value()) {
            throw std::runtime_error("ocean cloud planar reflection fallback is not initialized");
        }
        ocean_gpu_.update_cloud_planar_reflection_descriptor(
            device, frame_slot, cloud_reflection_fallback_texture_.value());
    }

    void refresh_cloud_weather_if_needed(const cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu) {
        if (!cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.update_weather_texture(
            device, gpu, cloud_runtime_shader_files().generated.weather,
            ocean_cloud_config(static_cast<float>(time_seconds_), ocean_surface_frame()));
    }

    void sync_cloud_runtime_after_ui(cubey::host::WindowedAppContext& context) {
        if (!clouds_config_.enabled || cloud_global_resources_created_) {
            return;
        }
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle ocean cloud enable");
        static_cast<void>(context.gpu().drain());
        create_cloud_resources(context.device(), context.gpu());
        create_cloud_pipelines(context.device(), context.swapchain().extent(),
                               context.frame_slot_count());
        create_cloud_environment_runtime(context.device(), context.frame_slot_count());
        create_cloud_planar_reflection(context.device(), context.swapchain().extent(),
                                       context.frame_slot_count());
        static_cast<void>(context.gpu().drain());
    }

    void create_reference_pillar_resources(cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu, VkExtent2D extent) {
        reference_pillar_pipeline_.reset();
        reference_pillar_mesh_.reset();

        const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal> mesh =
            make_ocean_reference_pillar_mesh();
        reference_pillar_mesh_.emplace(gpu, mesh.mesh_config());

        const cubey::render::VertexInputLayout vertex_layout =
            cubey::render::vertex_position_color_normal_input_layout();
        const std::filesystem::path shader_dir = CUBEY_OCEAN_SHADER_DIR;
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_dir / "ocean_reference_pillar.vert.spv"),
            cubey::render::fragment_shader_file(shader_dir / "ocean_reference_pillar.frag.spv"),
        };
        reference_pillar_pipeline_.emplace(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = extent,
                        .color_format = kOceanSceneColorFormat,
                        .depth_format = kOceanDepthFormat,
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_layout.bindings(),
                        .vertex_attributes = vertex_layout.attribute_descriptions(),
                        .material_pass = ocean_reference_pillar_pass_info(),
                    });
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        cloud_planar_reflection_.destroy();
        cloud_environment_runtime_.destroy();
        cloud_runtime_.destroy_swapchain_resources();
        cloud_runtime_.destroy_generated_resources();
        cloud_global_resources_created_ = false;
        hdr_post_frame_.destroy();
        reference_pillar_pipeline_.reset();
        reference_pillar_mesh_.reset();
        cloud_shadow_fallback_texture_.reset();
        cloud_reflection_fallback_texture_.reset();
        atmosphere_background_.destroy();
        ocean_gpu_.reset();
        atmosphere_runtime_.destroy();
        terrain_ocean_fields_texture_.reset();
        terrain_ocean_field_uniforms_.reset();
        terrain_ocean_fields_ = {};
        atmosphere_background_atlases_.reset();
        pipeline_color_format_ = VK_FORMAT_UNDEFINED;
        textures_initialized_ = false;
        spectrum_initialized_ = false;
        foam_initialized_ = false;
        gpu_config_.reset();
    }

    void sync_gpu_resources(cubey::host::WindowedAppContext& context) {
        validate_ocean_config(ocean_config_);
        if (!gpu_config_.has_value()) {
            create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
            return;
        }
        if (ocean_resource_layout_changed(ocean_config_, gpu_config_.value())) {
            cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                                 "vkDeviceWaitIdle before ocean resource recreation");
            create_pipeline(context.device(), context.gpu(), context.swapchain().format(),
                            context.swapchain().extent(), context.frame_slot_count());
            return;
        }
        if (ocean_terrain_field_source_changed(ocean_config_, gpu_config_.value())) {
            cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                                 "vkDeviceWaitIdle before ocean terrain field recreation");
            terrain_ocean_fields_texture_.reset();
            terrain_ocean_field_uniforms_.reset();
            create_terrain_ocean_field_resources(context.device(), context.gpu(),
                                                 context.frame_slot_count());
            ocean_gpu_.update_terrain_ocean_field_descriptor(context.device(),
                                                             terrain_ocean_fields_texture_.value());
            update_terrain_ocean_field_uniform_descriptors(context.device());
        }
        if (ocean_wave_source_changed(ocean_config_, gpu_config_.value())) {
            spectrum_initialized_ = false;
            foam_initialized_ = false;
            spectrum_diagnostics_ = ocean_spectrum_diagnostics(ocean_config_);
        }
        if (ocean_cloud_environment_resource_changed(ocean_config_, gpu_config_.value()) &&
            cloud_global_resources_created_) {
            cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                                 "vkDeviceWaitIdle before cloud environment recreation");
            create_cloud_environment_runtime(context.device(), context.frame_slot_count());
        }
        if (ocean_cloud_planar_resource_changed(ocean_config_, gpu_config_.value()) &&
            cloud_global_resources_created_) {
            cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                                 "vkDeviceWaitIdle before cloud planar reflection recreation");
            create_cloud_planar_reflection(context.device(), context.swapchain().extent(),
                                           context.frame_slot_count());
        }
        if (ocean_config_.cloud_reflection_source != gpu_config_->cloud_reflection_source) {
            cloud_environment_runtime_.invalidate();
        }
        gpu_config_ = ocean_config_;
        sync_cloud_runtime_after_ui(context);
        refresh_cloud_weather_if_needed(context.device(), context.gpu());
    }

    void apply_camera_preset(OceanCameraPreset preset) {
        const OceanCameraPresetConfig config = ocean_camera_preset_config(preset);
        camera_preset_ = config.preset;
        camera_base_yaw_ = config.yaw;
        camera_base_pitch_ = config.pitch;
        orbit_controller_.set_home_distance(config.distance);
        orbit_controller_.reset();
    }

    [[nodiscard]] cubey::Transform3D camera_transform() const {
        const float distance = orbit_controller_.distance();
        const float pitch = ocean_above_surface_orbit_pitch(
            distance, camera_base_pitch_ + orbit_controller_.pitch(),
            kCameraMinimumSurfaceClearanceMeters);
        return cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = {0.0F, 0.0F, 0.0F},
            .distance = distance,
            .yaw = camera_base_yaw_ + orbit_controller_.yaw(),
            .pitch = pitch,
        });
    }

    [[nodiscard]] float planet_radius_m() const {
        return ocean_planet_radius_m(atmosphere_state_.environment.bottom_radius_km) *
               ocean_config_.planet_radius_scale;
    }

    [[nodiscard]] OceanSurfaceFrame ocean_surface_frame() const {
        const cubey::Transform3D transform = camera_transform();
        return ocean_surface_frame_from_camera(ocean_config_, transform.translation,
                                               planet_radius_m());
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
    atmosphere_environment_for_surface_frame(const OceanSurfaceFrame& surface_frame) const {
        cubey::render::AtmosphereEnvironmentConfig environment = atmosphere_state_.environment;
        environment.camera_altitude_km =
            surface_frame.horizon.camera_altitude_m / kOceanMetersPerKilometer;
        return environment;
    }

    [[nodiscard]] cubey::math::Mat4 ocean_view_projection_matrix(
        VkExtent2D extent,
        const cubey::Transform3D& transform,
        float far_plane_m) const {
        cubey::Camera3D camera = camera_;
        camera.set_projection(camera.fovy_radians(), kCameraNearPlane,
                              std::max(far_plane_m, kCameraFarPlane));
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        return camera.view_projection_matrix(transform, aspect);
    }

    [[nodiscard]] cubey::math::Mat4 ocean_view_projection_matrix(
        VkExtent2D extent,
        const cubey::Transform3D& transform,
        const OceanSurfaceFrame& surface_frame) const {
        return ocean_view_projection_matrix(extent, transform, surface_frame.projection_far_plane_m);
    }

    [[nodiscard]] OceanMeshDrawPlan ocean_mesh_draw_plan(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        OceanMeshDrawPlan plan{};
        plan.surface_frame =
            ocean_surface_frame_from_camera(ocean_config_, transform.translation,
                                            planet_radius_m());
        plan.patches = ocean_mesh_clipmap_patches(plan.surface_frame.mesh_config);
        const cubey::math::Mat4 view_projection =
            ocean_view_projection_matrix(extent, transform, plan.surface_frame);
        const cubey::scene::Frustum3D frustum =
            cubey::scene::frustum_from_view_projection(view_projection);

        plan.stats.generated_patches = static_cast<std::uint32_t>(plan.patches.count);
        plan.stats.generated_triangles =
            ocean_mesh_total_triangle_count(plan.surface_frame.mesh_config);
        for (std::size_t index = 0; index < plan.patches.count; ++index) {
            const OceanMeshPatch& patch = plan.patches.patches[index];
            const cubey::Bounds3D bounds = ocean_mesh_patch_world_bounds(
                ocean_config_, plan.surface_frame, patch, transform.translation);
            const bool visible = cubey::scene::intersects(frustum, bounds);
            plan.visible_patches[index] = visible;
            if (visible) {
                ++plan.stats.submitted_patches;
                plan.stats.submitted_triangles += ocean_mesh_patch_triangle_count(patch);
            }
        }
        plan.stats.culled_patches =
            plan.stats.generated_patches - plan.stats.submitted_patches;
        return plan;
    }

    [[nodiscard]] OceanPushConstants surface_push_constants(VkExtent2D extent,
                                                            const OceanSurfaceFrame& surface_frame,
                                                            const OceanMeshPatch& patch) const {
        const cubey::Transform3D transform = camera_transform();
        const cubey::math::Mat4 view_projection =
            ocean_view_projection_matrix(extent, transform, surface_frame);
        const float debug_z = render_view_ == OceanRenderView::Exposure
                                  ? display_exposure()
                                  : static_cast<float>(surface_frame.mesh_config.mesh_lod_levels -
                                                       1U);

        return {
            .view_projection = view_projection,
            .camera_time =
                {
                    transform.translation.x,
                    transform.translation.y,
                    transform.translation.z,
                    static_cast<float>(time_seconds_),
                },
            .mesh_options =
                {
                    static_cast<float>(patch.cells_x),
                    static_cast<float>(patch.cells_z),
                    surface_frame.mesh_config.mesh_extent,
                    ocean_config_.horizon_fog,
                },
            .patch_bounds =
                {
                    patch.bounds.min_x,
                    patch.bounds.max_x,
                    patch.bounds.min_z,
                    patch.bounds.max_z,
                },
            .debug_options =
                {
                    static_cast<float>(static_cast<std::uint32_t>(render_view_)),
                    static_cast<float>(patch.level),
                    debug_z,
                    diagnostics_.wire_overlay ? std::clamp(diagnostics_.wire_opacity, 0.0F, 1.0F)
                                              : 0.0F,
                },
            .inspection_options =
                {
                    static_cast<float>(diagnostics_.selected_cascade),
                    ocean_config_.shape_anti_repeat_strength,
                    ocean_config_.foam_density,
                    ocean_config_.foam_sharpness,
                },
            .tile_lengths =
                {
                    ocean_config_.cascades[0].tile_length,
                    ocean_config_.cascades[1].tile_length,
                    ocean_config_.cascades[2].tile_length,
                    ocean_config_.cascades[3].tile_length,
                },
            .displacement_scales =
                {
                    ocean_config_.cascades[0].displacement_scale,
                    ocean_config_.cascades[1].displacement_scale,
                    ocean_config_.cascades[2].displacement_scale,
                    ocean_config_.cascades[3].displacement_scale,
                },
            .normal_scales =
                {
                    ocean_config_.cascades[0].normal_scale,
                    ocean_config_.cascades[1].normal_scale,
                    ocean_config_.cascades[2].normal_scale,
                    ocean_config_.cascades[3].normal_scale,
                },
            .cascade4_options =
                {
                    ocean_config_.cascades[4].tile_length,
                    ocean_config_.cascades[4].displacement_scale,
                    ocean_config_.cascades[4].normal_scale,
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, 4U)),
                },
            .water_color =
                {
                    ocean_config_.water_color_r,
                    ocean_config_.water_color_g,
                    ocean_config_.water_color_b,
                    ocean_config_.roughness,
                },
            .foam_color =
                {
                    ocean_config_.foam_color_r,
                    ocean_config_.foam_color_g,
                    ocean_config_.foam_color_b,
                    ocean_config_.normal_strength,
                },
        };
    }

    [[nodiscard]] OceanSurfaceFeatureUniforms surface_feature_uniforms(
        const OceanSurfaceFrame& surface_frame,
        const cubey::render::CloudLayerShadowProduct* cloud_shadow,
        cubey::render::FrameSlot frame_slot) const {
        const bool cloud_shadow_valid = cloud_shadow != nullptr && cloud_shadow->transmittance;
        const cubey::render::CloudEnvironmentProbeSnapshot cloud_environment =
            cloud_environment_runtime_.snapshot();
        const cubey::render::CloudPlanarReflectionSnapshot cloud_planar =
            cloud_planar_reflection_.resources_created()
                ? cloud_planar_reflection_.snapshot(frame_slot)
                : cubey::render::CloudPlanarReflectionSnapshot{};
        const bool selected_cloud_reflection_valid =
            ocean_config_.cloud_reflection_source == OceanCloudReflectionSource::Planar
                ? cloud_planar.valid || cloud_environment.valid
                : cloud_environment.valid;
        const cubey::math::Vec4 cloud_shadow_world_to_uv_x =
            cloud_shadow_valid ? cloud_shadow->world_to_uv_x : cubey::math::Vec4{};
        const cubey::math::Vec4 cloud_shadow_world_to_uv_y =
            cloud_shadow_valid ? cloud_shadow->world_to_uv_y : cubey::math::Vec4{};
        return {
            .feature_options =
                {
                    ocean_config_.surface_shape_strength,
                    ocean_config_.surface_foam_strength,
                    ocean_config_.foam_history_strength,
                    ocean_config_.detail_anti_repeat_strength,
                },
            .feature_options2 =
                {
                    ocean_config_.terrain_foam_strength,
                    ocean_config_.shape_fade_distance_scale,
                    ocean_config_.normal_fade_distance_scale,
                    ocean_config_.foam_fade_distance_scale,
                },
            .fade_options =
                {
                    0.0F,
                    diagnostics_.size_reference_enabled ? 1.0F : 0.0F,
                    0.72F,
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, 3U)),
                },
            .cascade_options =
                {
                    static_cast<float>(ocean_enabled_cascade_mask(ocean_config_)),
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, 0U)),
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, 1U)),
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, 2U)),
                },
            .self_shadow_options =
                {
                    ocean_config_.self_shadow_strength,
                    ocean_config_.self_shadow_distance,
                    ocean_config_.self_shadow_bias,
                    static_cast<float>(ocean_config_.self_shadow_steps),
                },
            .surface_frame_options =
                {
                    surface_frame.local_frame.water_datum_m,
                    surface_frame.local_frame.planet_radius_m,
                    surface_frame.horizon.camera_altitude_m,
                    surface_frame.horizon.horizon_distance_m,
                },
            .surface_curve_options =
                {
                    surface_frame.surface_mode == OceanSurfaceMode::CurvedFar ? 1.0F : 0.0F,
                    surface_frame.curvature_start_m,
                    surface_frame.curvature_end_m,
                    surface_frame.curvature_strength,
                },
            .far_field_options =
                {
                    ocean_config_.far_field_enabled ? 1.0F : 0.0F,
                    ocean_config_.far_field_start_m,
                    ocean_config_.far_field_end_m,
                    0.0F,
                },
            .far_field_options2 =
                {
                    ocean_config_.far_roughness_strength,
                    ocean_config_.far_glint_strength,
                    0.0F,
                    0.0F,
                },
            .far_detail_options =
                {
                    ocean_config_.far_detail_footprint_start_m,
                    ocean_config_.far_detail_footprint_end_m,
                    ocean_config_.far_reflection_variation_strength,
                    ocean_config_.sun_glitter_width,
                },
            .cloud_shadow_world_to_uv_x = cloud_shadow_world_to_uv_x,
            .cloud_shadow_world_to_uv_y = cloud_shadow_world_to_uv_y,
            .cloud_lighting_options =
                {
                    cloud_shadow_valid ? ocean_config_.cloud_shadow_strength : 0.0F,
                    cloud_shadow_valid ? 1.0F : 0.0F,
                    cloud_shadow_valid ? cloud_shadow->texel_world_size_m : 0.0F,
                    selected_cloud_reflection_valid ? ocean_config_.cloud_reflection_strength
                                                    : 0.0F,
                },
            .cloud_environment_options =
                {
                    static_cast<float>(static_cast<std::uint32_t>(
                        ocean_config_.cloud_reflection_source)),
                    cloud_environment.valid ? cloud_environment.blend : 1.0F,
                    cloud_environment.valid ? 1.0F : 0.0F,
                    4.0F,
                },
            .cloud_planar_right_aspect = cloud_planar.valid
                                              ? cloud_planar.view_rays.right_aspect
                                              : cubey::math::Vec4{},
            .cloud_planar_up_tan_half_fovy =
                cloud_planar.valid ? cloud_planar.view_rays.up_tan_half_fovy
                                   : cubey::math::Vec4{},
            .cloud_planar_forward_lod =
                cloud_planar.valid
                    ? cubey::math::Vec4{cloud_planar.view_rays.forward.x,
                                        cloud_planar.view_rays.forward.y,
                                        cloud_planar.view_rays.forward.z, cloud_planar.max_lod}
                    : cubey::math::Vec4{},
            .cloud_planar_options =
                {cloud_planar.valid ? 1.0F : 0.0F, ocean_config_.cloud_planar_guard_band,
                 0.0F, 0.0F},
            .sun_light_direction_intensity =
                {
                    atmosphere_lighting_.sun_direction.x,
                    atmosphere_lighting_.sun_direction.y,
                    atmosphere_lighting_.sun_direction.z,
                    atmosphere_lighting_.sun_intensity,
                },
            .sun_light_color =
                {
                    atmosphere_lighting_.sun_color.x,
                    atmosphere_lighting_.sun_color.y,
                    atmosphere_lighting_.sun_color.z,
                    0.0F,
                },
            .moon_light_direction_intensity =
                {
                    atmosphere_lighting_.moon_direction.x,
                    atmosphere_lighting_.moon_direction.y,
                    atmosphere_lighting_.moon_direction.z,
                    atmosphere_lighting_.moon_intensity,
                },
            .moon_light_color =
                {
                    atmosphere_lighting_.moon_color.x,
                    atmosphere_lighting_.moon_color.y,
                    atmosphere_lighting_.moon_color.z,
                    0.0F,
                },
        };
    }

    [[nodiscard]] OceanReferencePillarPushConstants
    reference_pillar_push_constants(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .light_direction = atmosphere_sun_light_uniform(),
        };
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    atmosphere_background_uniforms(VkExtent2D extent,
                                   const OceanSurfaceFrame& surface_frame) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::render::ViewRayBasis3D view_rays =
            cubey::render::view_ray_basis_3d(transform.rotation, aspect, camera_.fovy_radians());
        return cubey::render::atmosphere_environment_frame_uniforms(
            atmosphere_environment_for_surface_frame(surface_frame),
            cubey::render::AtmosphereEnvironmentFrameUniformInputs{
                .view_rays = view_rays,
                .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
            });
    }

    [[nodiscard]] cubey::render::CloudLayerFrameInfo
    cloud_frame_info(VkExtent2D extent, const OceanSurfaceFrame& surface_frame) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::render::ViewRayBasis3D view_rays =
            cubey::render::view_ray_basis_3d(transform.rotation, aspect, camera_.fovy_radians());
        const cubey::render::CloudLayerConfig config =
            ocean_cloud_config(static_cast<float>(time_seconds_), surface_frame);
        const cubey::render::CloudLayerViewRegime view_regime =
            cubey::render::cloud_layer_view_regime({
                .camera_position = {transform.translation.x,
                                    config.planet_radius_m + transform.translation.y,
                                    transform.translation.z},
                .camera_forward = cubey::math::Vec3{view_rays.forward},
                .planet_radius_m = config.planet_radius_m,
                .orbit_transition_start_m = config.orbit_transition_start_m,
                .orbit_transition_end_m = config.orbit_transition_end_m,
            });
        return cubey::render::CloudLayerFrameInfo{
                .camera_position = transform.translation,
                .camera_right = cubey::math::Vec3{view_rays.right_aspect},
                .camera_up = cubey::math::Vec3{view_rays.up_tan_half_fovy},
                .camera_forward = cubey::math::Vec3{view_rays.forward},
                .tan_half_fovy = view_rays.up_tan_half_fovy.w,
                .sun_direction = atmosphere_lighting_.sun_direction,
                .sun_color = atmosphere_lighting_.sun_color,
                .sun_intensity = atmosphere_lighting_.sun_intensity,
                .moon_direction = atmosphere_lighting_.moon_direction,
                .moon_color = atmosphere_lighting_.moon_color,
                .moon_intensity = atmosphere_lighting_.moon_intensity,
                .ambient_color = atmosphere_lighting_.ambient_color,
                .ambient_intensity = atmosphere_lighting_.ambient_intensity,
                .target_extent = extent,
                .temporal_frame_index = cloud_runtime_.temporal_frame_index(),
                .camera_mode = view_regime.camera_mode,
                .external_background = true,
                .near_plane_m = kCameraNearPlane,
                .far_plane_m = surface_frame.projection_far_plane_m,
                .scene_depth_occlusion_enabled = true,
                .scene_depth_fade_m = 500.0F,
            };
    }

    [[nodiscard]] cubey::render::CloudLayerFrameUniforms
    cloud_frame_uniforms(VkExtent2D extent, const OceanSurfaceFrame& surface_frame) const {
        return cubey::render::cloud_layer_frame_uniforms(
            ocean_cloud_config(static_cast<float>(time_seconds_), surface_frame),
            cloud_frame_info(extent, surface_frame));
    }

    [[nodiscard]] cubey::render::CloudLayerShadowRequest
    cloud_shadow_request(const OceanSurfaceFrame& surface_frame) const {
        const cubey::Transform3D transform = camera_transform();
        return {
            .receiver_center =
                {
                    transform.translation.x,
                    surface_frame.local_frame.water_datum_m,
                    transform.translation.z,
                },
            .receiver_axis_u = {1.0F, 0.0F, 0.0F},
            .receiver_axis_v = {0.0F, 0.0F, 1.0F},
            .half_extent_m = ocean_cloud_shadow_half_extent_m(orbit_controller_.distance()),
            .direct_light_direction = atmosphere_lighting_.sun_direction,
            .direct_light_intensity = atmosphere_lighting_.sun_intensity,
        };
    }

    [[nodiscard]] OceanSpectrumPushConstants
    spectrum_push_constants(std::uint32_t cascade_index) const {
        const OceanCascadeConfig& cascade = ocean_cascade(ocean_config_, cascade_index);
        const OceanCascadeDomain domain = ocean_config_.spectral_domains_enabled
                                              ? ocean_cascade_domain(ocean_config_, cascade_index)
                                              : OceanCascadeDomain{};
        const float fetch_m = cascade.fetch_length_km * 1000.0F;
        return {
            .seed_tile =
                {
                    static_cast<float>(cascade.seed_x),
                    static_cast<float>(cascade.seed_y),
                    cascade.tile_length,
                    cascade.tile_length,
                },
            .spectrum_options =
                {
                    ocean_jonswap_alpha(cascade.wind_speed, fetch_m),
                    ocean_jonswap_peak_frequency(cascade.wind_speed, fetch_m),
                    cascade.wind_speed,
                    radians(cascade.wind_direction_degrees),
                },
            .shape_options =
                {
                    ocean_config_.depth,
                    cascade.swell,
                    cascade.detail,
                    cascade.spread,
                },
            .cascade_options =
                {
                    static_cast<float>(cascade_index),
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, cascade_index)),
                    domain.active ? domain.low_k : 0.0F,
                    domain.active ? domain.high_k : 0.0F,
                },
        };
    }

    [[nodiscard]] cubey::math::Vec4 atmosphere_sun_light_uniform() const {
        const cubey::math::Vec3& light = atmosphere_lighting_.sun_direction;
        return {light.x, light.y, light.z, atmosphere_lighting_.sun_intensity};
    }

    [[nodiscard]] OceanModulatePushConstants
    modulate_push_constants(std::uint32_t cascade_index) const {
        const OceanCascadeConfig& cascade = ocean_cascade(ocean_config_, cascade_index);
        return {
            .tile_depth_time =
                {
                    cascade.tile_length,
                    cascade.tile_length,
                    ocean_config_.depth,
                    static_cast<float>(time_seconds_) + cascade.time_offset,
                },
            .cascade_options =
                {
                    static_cast<float>(cascade_index),
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, cascade_index)),
                    0.0F,
                    0.0F,
                },
        };
    }

    [[nodiscard]] OceanFftPushConstants fft_push_constants(std::uint32_t cascade_index,
                                                           std::uint32_t stage, bool horizontal,
                                                           bool first_pass) const {
        return {
            .fft_options =
                {
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, cascade_index)),
                    static_cast<float>(stage),
                    horizontal ? 1.0F : 0.0F,
                    first_pass ? 1.0F : 0.0F,
                },
            .pass_options = {0.0F, 0.0F, 0.0F, 0.0F},
        };
    }

    [[nodiscard]] OceanUnpackPushConstants
    unpack_push_constants(std::uint32_t cascade_index) const {
        const OceanCascadeConfig& cascade = ocean_cascade(ocean_config_, cascade_index);
        const float delta_seconds =
            static_cast<float>(last_delta_seconds_ > 0.0 ? last_delta_seconds_ : (1.0 / 60.0));
        const float foam_grow_rate = delta_seconds * cascade.foam_amount * 7.5F;
        const float foam_decay_rate =
            delta_seconds * std::max(0.5F, 10.0F - cascade.foam_amount) * 1.15F;
        return {
            .foam_options =
                {
                    cascade.whitecap,
                    foam_grow_rate,
                    foam_decay_rate,
                    foam_initialized_ ? 1.0F : 0.0F,
                },
            .cascade_options =
                {
                    static_cast<float>(cascade_index),
                    static_cast<float>(ocean_cascade_map_size(ocean_config_, cascade_index)),
                    0.0F,
                    0.0F,
                },
        };
    }

    void record_atmosphere_background(const cubey::vulkan::CommandRecorder& recorder,
                                      VkExtent2D extent,
                                      cubey::render::FrameSlot frame_slot) const {
        const OceanSurfaceFrame surface_frame = ocean_surface_frame();
        atmosphere_background_.upload(frame_slot, atmosphere_background_uniforms(extent,
                                                                                surface_frame));
        cubey::render::record_fullscreen_pipeline_draw(
            recorder, cubey::render::FullscreenPipelineDrawInfo{
                          .pipeline = &atmosphere_background_.pipeline(),
                          .descriptor_set = atmosphere_background_.material().set(frame_slot),
                          .descriptor_set_index = 0,
                      });
    }

    void record_ocean_draw(
        const cubey::vulkan::CommandRecorder& recorder, VkExtent2D extent,
        cubey::render::FrameSlot frame_slot,
        const cubey::render::CloudLayerShadowProduct* cloud_shadow) const {
        const OceanMeshDrawPlan draw_plan = ocean_mesh_draw_plan(extent);
        const cubey::render::GraphicsPipelineResource& surface_pipeline =
            ocean_gpu_.surface_pipeline();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pipeline.pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pipeline.layout(), 0,
                                     ocean_gpu_.surface_set(frame_slot));
        ocean_gpu_.upload_surface_feature_uniforms(
            frame_slot,
            surface_feature_uniforms(draw_plan.surface_frame, cloud_shadow, frame_slot));
        for (std::size_t index = 0; index < draw_plan.patches.count; ++index) {
            if (!draw_plan.visible_patches[index]) {
                continue;
            }
            const OceanMeshPatch& patch = draw_plan.patches.patches[index];
            const OceanPushConstants constants =
                surface_push_constants(extent, draw_plan.surface_frame, patch);
            recorder.push_constants(surface_pipeline.layout(),
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    constants);
            recorder.draw(ocean_mesh_patch_vertex_count(patch));
        }
    }

    void record_reference_pillar_draw(const cubey::vulkan::CommandRecorder& recorder,
                                      VkExtent2D extent) const {
        if (!diagnostics_.size_reference_enabled || render_view_ != OceanRenderView::Final) {
            return;
        }
        if (!reference_pillar_pipeline_.has_value() || !reference_pillar_mesh_.has_value()) {
            throw std::runtime_error("ocean reference pillar resources are not initialized");
        }
        const cubey::render::GraphicsPipelineResource& pipeline =
            reference_pillar_pipeline_.value();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
        recorder.push_constants(pipeline.layout(),
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                reference_pillar_push_constants(extent));
        cubey::render::record_draw_item(recorder.handle(), {
                                                               .mesh = &reference_pillar_mesh_.value(),
                                                           });
    }

    [[nodiscard]] cubey::render::PbrPostUniforms post_uniforms() const {
        return cubey::render::hdr_post_uniforms(pipeline_color_format_, display_exposure());
    }

    [[nodiscard]] float display_exposure() const {
        if (atmosphere_state_.auto_exposure_enabled && !config_.pbr.exposure_explicit) {
            return atmosphere_state_.resolved_exposure;
        }
        return ocean_config_.exposure;
    }

    void record_ocean_post(const cubey::vulkan::CommandRecorder& recorder,
                           cubey::render::ColorTargetView target,
                           cubey::render::FrameSlot frame_slot) const {
        hdr_post_frame_.record_pass(recorder, target, frame_slot);
    }

    [[nodiscard]] OceanFrameGraph
    build_ocean_frame_graph(cubey::render::ColorTargetView color_target,
                            cubey::render::FrameSlot frame_slot,
                            OceanRenderTargetMode target_mode,
                            std::optional<cubey::render::CloudLayerFrameUniforms>
                                cloud_uniforms) const {
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureState initial_state =
            target_mode == OceanRenderTargetMode::Present
                ? cubey::render::render_graph_undefined_texture_state()
                : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureState final_state =
            target_mode == OceanRenderTargetMode::Present
                ? cubey::render::render_graph_present_texture_state()
                : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureHandle backbuffer =
            graph.import_color_target("ocean backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "ocean scene color", color_target.extent, kOceanSceneColorFormat));
        cubey::render::RenderGraphTextureHandle post_scene_color = scene_color;
        cubey::render::RenderGraphTextureHandle cloud_scene_color{};
        cubey::render::CloudLayerRuntimeFrame cloud_frame{};
        cubey::render::CloudLayerShadowProduct cloud_shadow{};
        const cubey::render::RenderGraphTextureHandle surface_depth =
            graph.create_texture(ocean_depth_texture_desc("ocean surface depth",
                                                          color_target.extent, kOceanDepthFormat));
        const bool cloud_product_enabled = cloud_products_enabled() && cloud_uniforms.has_value();
        const bool cloud_shadow_enabled =
            cloud_product_enabled &&
            (ocean_config_.cloud_shadow_strength > 0.0F ||
             render_view_ == OceanRenderView::CloudShadow);
        const bool visible_clouds_enabled = cloud_composite_enabled() && cloud_uniforms.has_value();
        const bool cloud_march_enabled = visible_clouds_enabled;
        const OceanSurfaceFrame surface_frame = ocean_surface_frame();
        if (cloud_shadow_enabled) {
            cloud_shadow = cloud_runtime_.declare_shadow_product(
                graph, frame_slot, cloud_shadow_request(surface_frame));
        }
        if (cloud_march_enabled) {
            cloud_frame = cloud_runtime_.declare_product(
                graph, color_target.extent,
                ocean_cloud_config(static_cast<float>(time_seconds_), surface_frame), frame_slot,
                cloud_uniforms.value());
        }
        if (visible_clouds_enabled) {
            cloud_scene_color = graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "ocean cloud scene color", color_target.extent, kOceanSceneColorFormat));
        }

        auto scene_pass =
            graph.add_pass("ocean scene", cubey::render::RenderGraphQueueDomain::Graphics);
        if (cloud_shadow_enabled) {
            scene_pass.read_texture(cloud_shadow.transmittance,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        scene_pass
            .write_color(scene_color)
            .write_depth(surface_depth)
            .execute([this, scene_color, frame_slot, surface_depth, cloud_shadow,
                      cloud_shadow_enabled](
                         const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView target =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView depth =
                    cubey::render::resolved_depth_target_view(context, surface_depth);
                cubey::render::record_render_target_pass(
                    context.recorder(), cubey::render::render_target_view(target, depth),
                    cubey::render::RenderClearValues{
                        .color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
                    [this, target, frame_slot, cloud_shadow, cloud_shadow_enabled](
                        const cubey::vulkan::CommandRecorder& draw_recorder) {
                        record_atmosphere_background(draw_recorder, target.extent, frame_slot);
                        record_ocean_draw(draw_recorder, target.extent, frame_slot,
                                          cloud_shadow_enabled ? &cloud_shadow : nullptr);
                        record_reference_pillar_draw(draw_recorder, target.extent);
                    });
            });
        if (visible_clouds_enabled) {
            cloud_runtime_.declare_composite(graph, cloud_scene_color, cloud_frame, frame_slot,
                                             scene_color, surface_depth);
            post_scene_color = cloud_scene_color;
        }
        graph.add_pass("ocean post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(post_scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, color_target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_ocean_post(context.recorder(), color_target, frame_slot);
            });

        return {
            .graph = graph.compile(),
            .backbuffer = backbuffer,
            .post_scene_color = post_scene_color,
            .scene_color = scene_color,
            .surface_depth = surface_depth,
            .cloud_scene_color = cloud_scene_color,
            .cloud = cloud_frame,
            .cloud_shadow = cloud_shadow,
            .cloud_product_enabled = cloud_march_enabled,
            .cloud_shadow_enabled = cloud_shadow_enabled,
            .cloud_composite_enabled = visible_clouds_enabled,
        };
    }

    void update_post_descriptor(const cubey::vulkan::Device& device,
                                cubey::render::FrameSlot frame_slot,
                                const cubey::render::CompiledRenderGraph& graph,
                                const cubey::render::RenderGraphResourceSet& resources,
                                cubey::render::RenderGraphTextureHandle scene_color) const {
        hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, graph, resources,
                                                      scene_color);
    }

    void record_initial_texture_transitions(const cubey::vulkan::CommandRecorder& recorder) const {
        recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
            ocean_gpu_.fallback_field().handle()));
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_gpu_.cascade_allocated(cascade)) {
                continue;
            }
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.h0(cascade).handle()));
            for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(
                        ocean_gpu_.field(cascade, field).handle()));
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(
                        ocean_gpu_.ping(cascade, field).handle()));
                recorder.transition_image_layout(
                    cubey::vulkan::begin_storage_image_write_transition(
                        ocean_gpu_.pong(cascade, field).handle()));
            }
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.displacement(cascade).handle()));
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.normal(cascade).handle()));
            recorder.transition_image_layout(cubey::vulkan::begin_storage_image_write_transition(
                ocean_gpu_.foam(cascade).handle()));
        }
    }

    void record_spectrum_init(const cubey::vulkan::CommandRecorder& recorder,
                              cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                             !foam_initialized_)) {
                continue;
            }
            const cubey::render::ComputeDispatchGroups groups =
                ocean_dispatch_groups(ocean_config_, cascade);
            const std::string profile_label = ocean_gpu_timing_label("spectrum", cascade);
            cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                                   profile_label);
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.spectrum_pipeline(), ocean_gpu_.spectrum_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, spectrum_push_constants(cascade));
        }
    }

    void record_modulate(const cubey::vulkan::CommandRecorder& recorder,
                         cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                             !foam_initialized_)) {
                continue;
            }
            const cubey::render::ComputeDispatchGroups groups =
                ocean_dispatch_groups(ocean_config_, cascade);
            const std::string profile_label = ocean_gpu_timing_label("modulate", cascade);
            cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                                   profile_label);
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.modulate_pipeline(), ocean_gpu_.modulate_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, modulate_push_constants(cascade));
        }
    }

    void record_fft_pass(const cubey::vulkan::CommandRecorder& recorder, std::uint32_t cascade,
                         std::uint32_t field, std::uint32_t stage, bool horizontal, bool first_pass,
                         std::uint32_t descriptor_set_index) const {
        const cubey::render::ComputeDispatchGroups groups =
            ocean_dispatch_groups(ocean_config_, cascade);
        cubey::render::record_compute_pipeline_dispatch(
            recorder,
            cubey::render::compute_pipeline_dispatch_info(
                ocean_gpu_.fft_pipeline(),
                ocean_gpu_.fft_set(cascade, field, descriptor_set_index), groups),
            VK_SHADER_STAGE_COMPUTE_BIT,
            fft_push_constants(cascade, stage, horizontal, first_pass));
    }

    void record_fft(const cubey::vulkan::CommandRecorder& recorder,
                    cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                             !foam_initialized_)) {
                continue;
            }
            const std::uint32_t stage_count =
                log2_exact(ocean_cascade_map_size(ocean_config_, cascade));
            const std::string profile_label = ocean_gpu_timing_label("fft", cascade);
            cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                                   profile_label);
            for (std::uint32_t field = 0; field < kOceanSpectrumFieldCount; ++field) {
                bool source_is_ping = true;
                for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                    const std::uint32_t set_index = stage == 1U ? 0U : (source_is_ping ? 1U : 2U);
                    record_fft_pass(recorder, cascade, field, stage, true, stage == 1U, set_index);
                    cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                    source_is_ping = stage == 1U ? true : !source_is_ping;
                }

                for (std::uint32_t stage = 1; stage <= stage_count; ++stage) {
                    const std::uint32_t set_index = source_is_ping ? 1U : 2U;
                    record_fft_pass(recorder, cascade, field, stage, false, stage == 1U, set_index);
                    cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
                    source_is_ping = !source_is_ping;
                }
            }
        }
    }

    void record_unpack(const cubey::vulkan::CommandRecorder& recorder,
                       cubey::render::FrameSlot frame_slot) {
        cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler();
        for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
            if (!ocean_should_update_cascade(ocean_config_, cascade, ocean_compute_frame_index_,
                                             !foam_initialized_)) {
                continue;
            }
            const cubey::render::ComputeDispatchGroups groups =
                ocean_dispatch_groups(ocean_config_, cascade);
            const std::string profile_label = ocean_gpu_timing_label("unpack", cascade);
            cubey::vulkan::GpuTimestampScope scope(profiler, recorder.handle(), frame_slot.index,
                                                   profile_label);
            cubey::render::record_compute_pipeline_dispatch(
                recorder,
                cubey::render::compute_pipeline_dispatch_info(
                    ocean_gpu_.unpack_pipeline(), ocean_gpu_.unpack_set(cascade), groups),
                VK_SHADER_STAGE_COMPUTE_BIT, unpack_push_constants(cascade));
        }
    }

    void record_ocean_compute(const cubey::vulkan::CommandRecorder& recorder,
                              cubey::render::FrameSlot frame_slot) {
        if (!textures_initialized_) {
            record_initial_texture_transitions(recorder);
            textures_initialized_ = true;
        }
        if (!spectrum_initialized_) {
            record_spectrum_init(recorder, frame_slot);
            cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
            spectrum_initialized_ = true;
        }
        record_modulate(recorder, frame_slot);
        cubey::vulkan::record_compute_shader_write_barrier(recorder.handle());
        record_fft(recorder, frame_slot);
        record_unpack(recorder, frame_slot);
        cubey::vulkan::record_shader_write_barrier(recorder.handle(),
                                                   VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                   VK_ACCESS_SHADER_READ_BIT);
        foam_initialized_ = true;
        ++ocean_compute_frame_index_;
    }

    void record_atmosphere_environment_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                                 cubey::render::FrameSlot frame_slot) {
        if (!atmosphere_runtime_.resources_created()) {
            throw std::runtime_error("ocean atmosphere runtime is not initialized");
        }
        atmosphere_runtime_.record_pending_update(recorder, frame_slot);
    }

    void record_cloud_environment_if_needed(const cubey::vulkan::CommandRecorder& recorder,
                                            cubey::render::FrameSlot frame_slot,
                                            VkExtent2D target_extent) {
        if (!cloud_environment_active()) {
            return;
        }
        const OceanSurfaceFrame surface_frame = ocean_surface_frame();
        static_cast<void>(cloud_environment_runtime_.record_pending_update(
            recorder,
            cubey::render::CloudEnvironmentProbeUpdateInfo{
                .frame_slot = frame_slot,
                .cloud = ocean_cloud_config(static_cast<float>(time_seconds_), surface_frame),
                .frame = cloud_frame_info(target_extent, surface_frame),
            }));
    }

    void record_cloud_planar_reflection_if_needed(
        const cubey::vulkan::CommandRecorder& recorder,
        cubey::render::FrameSlot frame_slot, VkExtent2D target_extent) {
        if (!cloud_planar_reflection_active()) {
            return;
        }
        const OceanSurfaceFrame surface_frame = ocean_surface_frame();
        cloud_planar_reflection_.record(
            recorder,
            cubey::render::CloudPlanarReflectionRequest{
                .frame_slot = frame_slot,
                .cloud = ocean_cloud_config(static_cast<float>(time_seconds_), surface_frame),
                .frame = cloud_frame_info(target_extent, surface_frame),
                .plane_point = {0.0F, surface_frame.local_frame.water_datum_m, 0.0F},
                .plane_normal = {0.0F, 1.0F, 0.0F},
            });
    }

    void update_cloud_environment_descriptors(const cubey::vulkan::Device& device,
                                              cubey::render::FrameSlot frame_slot) {
        const cubey::render::CloudEnvironmentProbeSnapshot snapshot =
            cloud_environment_runtime_.snapshot();
        if (snapshot.valid) {
            ocean_gpu_.update_cloud_environment_descriptors(
                device, frame_slot, *snapshot.previous, *snapshot.current);
            return;
        }
        const cubey::render::TextureCube& fallback =
            atmosphere_runtime_.reflection_probe().prefiltered_cube();
        ocean_gpu_.update_cloud_environment_descriptors(device, frame_slot, fallback, fallback);
    }

    void record_ocean_target(VkCommandBuffer command_buffer, const cubey::vulkan::Device& device,
                             cubey::render::ColorTargetView target,
                             cubey::render::FrameSlot frame_slot,
                             OceanRenderTargetMode target_mode) {
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        if (cubey::vulkan::GpuTimestampProfiler* profiler = ocean_gpu_.profiler()) {
            profiler->begin_frame(command_buffer, frame_slot.index);
        }
        {
            cubey::vulkan::GpuTimestampScope scope(ocean_gpu_.profiler(), command_buffer,
                                                   frame_slot.index, "ocean.atmosphere");
            record_atmosphere_environment_if_needed(recorder, frame_slot);
        }
        {
            cubey::vulkan::GpuTimestampScope scope(ocean_gpu_.profiler(), command_buffer,
                                                   frame_slot.index,
                                                   "ocean.cloud_environment");
            record_cloud_environment_if_needed(recorder, frame_slot, target.extent);
        }
        update_cloud_environment_descriptors(device, frame_slot);
        {
            cubey::vulkan::GpuTimestampScope scope(
                ocean_gpu_.profiler(), command_buffer, frame_slot.index,
                "ocean.cloud_planar_reflection");
            record_cloud_planar_reflection_if_needed(recorder, frame_slot, target.extent);
        }
        update_cloud_planar_reflection_descriptor(device, frame_slot);
        record_ocean_compute(recorder, frame_slot);
        upload_terrain_ocean_field_uniform(frame_slot);
        hdr_post_frame_.upload(frame_slot, post_uniforms());
        std::optional<cubey::render::CloudLayerFrameUniforms> cloud_uniforms{};
        if (cloud_products_enabled()) {
            const OceanSurfaceFrame surface_frame = ocean_surface_frame();
            cloud_uniforms = cloud_frame_uniforms(target.extent, surface_frame);
            cloud_runtime_.upload_frame_uniforms(frame_slot, cloud_uniforms.value());
        }
        const OceanFrameGraph frame_graph =
            build_ocean_frame_graph(target, frame_slot, target_mode, cloud_uniforms);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer ocean graph",
                .command_buffer_mode =
                    cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
                .profiler = ocean_gpu_.profiler(),
            },
            frame_graph.graph,
            [this, &device, frame_slot,
             &frame_graph](const cubey::render::RenderGraphResourceSet& resources) {
                update_post_descriptor(device, frame_slot, frame_graph.graph, resources,
                                       frame_graph.post_scene_color);
                if (frame_graph.cloud_shadow_enabled) {
                    cloud_runtime_.update_shadow_descriptors(
                        device, frame_slot, frame_graph.graph, resources, frame_graph.cloud_shadow);
                    const cubey::render::RenderGraphSampledTextureView transmittance =
                        cubey::render::resolved_sampled_texture_view(
                            frame_graph.graph, resources,
                            frame_graph.cloud_shadow.transmittance);
                    ocean_gpu_.update_cloud_shadow_descriptor(
                        device, frame_slot, cloud_runtime_.shadow_sampler().handle(),
                        transmittance.view, transmittance.layout);
                } else {
                    update_cloud_shadow_fallback_descriptor(device, frame_slot);
                }
                if (frame_graph.cloud_product_enabled) {
                    cloud_runtime_.update_product_descriptors(
                        device, frame_slot, frame_graph.graph, resources, frame_graph.cloud);
                }
                if (frame_graph.cloud_composite_enabled) {
                    cloud_runtime_.update_composite_descriptors(
                        device, frame_slot, frame_graph.graph, resources, frame_graph.cloud,
                        frame_graph.scene_color, frame_graph.surface_depth);
                }
            });
        if (frame_graph.cloud_product_enabled) {
            cloud_runtime_.complete_frame(frame_slot, frame_graph.cloud);
        }
    }

    void record_windowed_frame(cubey::host::WindowedAppContext& context,
                               const cubey::host::WindowedRenderFrame& frame) {
        collect_gpu_timings(context.profile_recorder(), windowed_frame_index_, frame.frame_slot,
                            time_seconds_);
        const cubey::vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        record_ocean_target(frame.command_buffer, context.device(), frame.color_target,
                            frame.frame_slot, OceanRenderTargetMode::Present);
        recorder.end("vkEndCommandBuffer ocean");
        ++windowed_frame_index_;
    }

    RunConfig config_;
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
    cubey::render::CloudLayerRuntime cloud_runtime_{};
    cubey::CloudEnvironmentRuntime cloud_environment_runtime_{};
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
    bool cloud_global_resources_created_ = false;
};

} // namespace

int run_ocean(const RunConfig& config) {
    OceanApp app(config);
    return app.run();
}

} // namespace cubey::projects::ocean
