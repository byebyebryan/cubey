#include "ocean_app_internal.h"

namespace cubey::projects::ocean::detail {

std::uint64_t collected_profile_frame_index(std::uint64_t frame_index,
                                            cubey::render::FrameSlot frame_slot) {
    cubey::render::validate_frame_slot(frame_slot);
    if (frame_index >= frame_slot.count) {
        return frame_index - frame_slot.count;
    }
    return 0;
}

float ocean_patch_wave_cull_margin_m(const OceanConfig& config) {
    float scale_sum = 0.0F;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (ocean_cascade_enabled(config, cascade)) {
            scale_sum += ocean_cascade(config, cascade).displacement_scale;
        }
    }
    return std::max(96.0F, scale_sum * 24.0F * std::max(config.surface_shape_strength, 0.0F));
}

cubey::Bounds3D ocean_mesh_patch_world_bounds(const OceanConfig& config,
                                              const OceanSurfaceFrame& surface_frame,
                                              const OceanMeshPatch& patch,
                                              cubey::math::Vec3 camera_position_m) {
    const float snap = ocean_mesh_patch_snap_size(patch);
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
        const float distance = glm::length(
            cubey::math::Vec2{corner.x - camera_position_m.x, corner.y - camera_position_m.z});
        min_drop =
            std::min(min_drop, ocean_surface_curvature_drop_m(
                                   distance, surface_frame.local_frame.planet_radius_m,
                                   surface_frame.curvature_start_m, surface_frame.curvature_end_m,
                                   surface_frame.curvature_strength));
    }

    const float water_datum = surface_frame.local_frame.water_datum_m;
    const float min_y = water_datum + min_drop - vertical_margin;
    const float max_y = water_datum + vertical_margin;
    return {
        .center = {(min_x + max_x) * 0.5F, (min_y + max_y) * 0.5F, (min_z + max_z) * 0.5F},
        .half_extent = {(max_x - min_x) * 0.5F, (max_y - min_y) * 0.5F, (max_z - min_z) * 0.5F},
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

std::string ocean_gpu_timing_label(const char* phase, std::uint32_t cascade) {
    return std::string("ocean.") + phase + ".c" + std::to_string(cascade);
}

cubey::render::PrimitiveVec3 linear_srgb(cubey::render::PrimitiveVec3 srgb_color) {
    return cubey::render::srgb_to_linear_rgb(srgb_color);
}

cubey::render::PrimitiveVec3 pillar_position(float local_u, float y, float local_v) {
    return {
        kReferencePillarCenterXMeters + local_u * kReferencePillarAxisUX +
            local_v * kReferencePillarAxisVX,
        y,
        kReferencePillarCenterZMeters + local_u * kReferencePillarAxisUZ +
            local_v * kReferencePillarAxisVZ,
    };
}

cubey::render::PrimitiveVec3 pillar_u_normal(float sign) {
    return {sign * kReferencePillarAxisUX, 0.0F, sign * kReferencePillarAxisUZ};
}

cubey::render::PrimitiveVec3 pillar_v_normal(float sign) {
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
    mesh.indices.insert(mesh.indices.end(), {base, static_cast<std::uint16_t>(base + 1U),
                                             static_cast<std::uint16_t>(base + 2U), base,
                                             static_cast<std::uint16_t>(base + 2U),
                                             static_cast<std::uint16_t>(base + 3U)});
}

void append_pillar_band(
    cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>& mesh,
    float half_width, float min_y, float max_y, cubey::render::PrimitiveVec3 color, bool bottom_cap,
    bool top_cap) {
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
                           pillar_position(-half_width, min_y, half_width), {0.0F, -1.0F, 0.0F},
                           color);
    }
    if (top_cap) {
        append_pillar_quad(mesh, pillar_position(-half_width, max_y, half_width),
                           pillar_position(half_width, max_y, half_width),
                           pillar_position(half_width, max_y, -half_width),
                           pillar_position(-half_width, max_y, -half_width), {0.0F, 1.0F, 0.0F},
                           color);
    }
}

cubey::render::PrimitiveVec3 reference_pillar_marker_color(int meter) {
    if (meter % 10 == 0) {
        return linear_srgb({1.0F, 0.08F, 0.05F});
    }
    if (meter % 5 == 0) {
        return linear_srgb({1.0F, 0.78F, 0.10F});
    }
    return linear_srgb({0.015F, 0.015F, 0.015F});
}

float reference_pillar_marker_half_height(int meter) {
    if (meter % 10 == 0) {
        return kReferencePillarTenMeterMarkerHalfHeightMeters;
    }
    if (meter % 5 == 0) {
        return kReferencePillarFiveMeterMarkerHalfHeightMeters;
    }
    return kReferencePillarMeterMarkerHalfHeightMeters;
}

cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal>
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
        const float min_y = std::max(kReferencePillarMinYMeters, center_y - half_height);
        const float max_y = std::min(kReferencePillarMaxYMeters, center_y + half_height);
        append_pillar_band(mesh, kReferencePillarMarkerHalfWidthMeters, min_y, max_y,
                           reference_pillar_marker_color(marker), false, false);
    }
    return mesh;
}

cubey::render::MaterialPassInfo ocean_reference_pillar_pass_info() {
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

float radians(float degrees) {
    return degrees * (std::numbers::pi_v<float> / 180.0F);
}

cubey::AtmosphereEnvironmentRunState
ocean_atmosphere_run_state(const OceanStartupOptions& run_config) {
    return cubey::atmosphere_environment_run_state_from_config(
        run_config.atmosphere,
        {
            .sun_elevation_degrees = kOceanSunElevationDegrees,
            .sun_azimuth_degrees = kOceanSunAzimuthDegrees,
            .ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion,
            .reference_geometry_enabled = false,
        });
}

cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files() {
    return cubey::render::cloud_layer_runtime_shader_files(
        std::filesystem::path(CUBEY_OCEAN_SHADER_DIR),
        cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
}

std::uint32_t log2_exact(std::uint32_t value) {
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

cubey::render::ComputeDispatchGroups ocean_dispatch_groups(const OceanConfig& config,
                                                           std::uint32_t cascade) {
    const std::uint32_t map_size = ocean_cascade_map_size(config, cascade);
    return cubey::render::ceil_dispatch_groups(map_size, map_size, 16U);
}

cubey::render::RenderGraphTextureDesc ocean_depth_texture_desc(const char* label, VkExtent2D extent,
                                                               VkFormat format) {
    return {
        .label = label,
        .extent = {extent.width, extent.height, 1},
        .format = format,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

OceanCameraPresetConfig ocean_camera_preset_config(OceanCameraPreset preset) {
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

OceanCameraPreset ocean_camera_preset_from_name(std::string_view name) {
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

bool ocean_resource_layout_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.map_size != rhs.map_size || lhs.field_precision != rhs.field_precision ||
           lhs.cascade_enabled != rhs.cascade_enabled ||
           lhs.cascade_map_sizes != rhs.cascade_map_sizes;
}

bool ocean_terrain_field_source_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.mesh_extent != rhs.mesh_extent || lhs.depth != rhs.depth;
}

bool ocean_wave_source_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.depth != rhs.depth || lhs.spectral_domains_enabled != rhs.spectral_domains_enabled ||
           lhs.cascades != rhs.cascades || lhs.cascade_enabled != rhs.cascade_enabled;
}

bool ocean_cloud_environment_resource_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.cloud_environment_extent != rhs.cloud_environment_extent ||
           lhs.cloud_environment_update_hz != rhs.cloud_environment_update_hz;
}

bool ocean_cloud_planar_resource_changed(const OceanConfig& lhs, const OceanConfig& rhs) {
    return lhs.cloud_planar_resolution_scale != rhs.cloud_planar_resolution_scale ||
           lhs.cloud_planar_view_steps != rhs.cloud_planar_view_steps ||
           lhs.cloud_planar_guard_band != rhs.cloud_planar_guard_band;
}

std::uint32_t ocean_enabled_cascade_mask(const OceanConfig& config) {
    std::uint32_t mask = 0U;
    for (std::uint32_t cascade = 0; cascade < kOceanCascadeCount; ++cascade) {
        if (ocean_cascade_enabled(config, cascade)) {
            mask |= 1U << cascade;
        }
    }
    return mask;
}

bool ocean_should_update_cascade(const OceanConfig& config, std::uint32_t cascade,
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

OceanTerrainFieldUniforms
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

cubey::render::Texture2D
create_ocean_cloud_shadow_fallback_texture(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu) {
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

cubey::render::Texture2D
create_ocean_cloud_reflection_fallback_texture(const cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu) {
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

cubey::render::TerrainOceanPackedFields
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
} // namespace cubey::projects::ocean::detail
