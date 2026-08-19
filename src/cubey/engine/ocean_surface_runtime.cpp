#include <cubey/engine/ocean_surface_runtime.h>

#include <cubey/render/color_space.h>
#include <cubey/render/material.h>
#include <cubey/render/pass.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/memory_barriers.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace cubey {
namespace {

using render::OceanFieldPrecision;

struct OceanSurfacePushConstants {
    math::Mat4 view_projection;
    math::Vec4 camera_time;
    math::Vec4 mesh_options;
    math::Vec4 patch_bounds;
    math::Vec4 debug_options;
    math::Vec4 inspection_options;
    math::Vec4 tile_lengths;
    math::Vec4 displacement_scales;
    math::Vec4 normal_scales;
    math::Vec4 cascade4_options;
    math::Vec4 water_color;
    math::Vec4 foam_color;
    math::Vec4 quality_options;
};

struct OceanSpectrumPushConstants {
    math::Vec4 seed_tile;
    math::Vec4 spectrum_options;
    math::Vec4 shape_options;
    math::Vec4 cascade_options;
};

struct OceanModulatePushConstants {
    math::Vec4 tile_depth_time;
    math::Vec4 cascade_options;
};

struct OceanFftPushConstants {
    math::Vec4 fft_options;
    math::Vec4 pass_options;
};

struct OceanUnpackPushConstants {
    math::Vec4 foam_options;
    math::Vec4 cascade_options;
};

static_assert(sizeof(OceanSurfacePushConstants) == sizeof(float) * 64U);
static_assert(sizeof(OceanSpectrumPushConstants) == sizeof(float) * 16U);
static_assert(sizeof(OceanModulatePushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanFftPushConstants) == sizeof(float) * 8U);
static_assert(sizeof(OceanUnpackPushConstants) == sizeof(float) * 8U);

constexpr std::uint32_t kOceanGpuProfilerPassCapacity = 64U;
constexpr std::uint32_t kOceanSurfaceReflectionBinding = cubey::render::kOceanCascadeCount * 3U;
constexpr std::uint32_t kOceanSurfaceSkyRadianceBinding = kOceanSurfaceReflectionBinding + 1U;
constexpr std::uint32_t kOceanSurfaceTerrainFieldBinding = kOceanSurfaceSkyRadianceBinding + 1U;
constexpr std::uint32_t kOceanSurfaceTerrainFieldUniformBinding =
    kOceanSurfaceTerrainFieldBinding + 1U;
constexpr std::uint32_t kOceanSurfaceFeatureUniformBinding =
    kOceanSurfaceTerrainFieldUniformBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudShadowBinding = kOceanSurfaceFeatureUniformBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudEnvironmentPreviousBinding =
    kOceanSurfaceCloudShadowBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudEnvironmentCurrentBinding =
    kOceanSurfaceCloudEnvironmentPreviousBinding + 1U;
constexpr std::uint32_t kOceanSurfaceCloudPlanarReflectionBinding =
    kOceanSurfaceCloudEnvironmentCurrentBinding + 1U;
constexpr std::uint32_t kOceanSurfaceReflectionCurrentBinding =
    kOceanSurfaceCloudPlanarReflectionBinding + 1U;
constexpr std::uint32_t kOceanSurfaceBindingCount = kOceanSurfaceReflectionCurrentBinding + 1U;

[[nodiscard]] std::filesystem::path shader_path(const std::filesystem::path& shader_dir,
                                                const char* filename) {
    return shader_dir / filename;
}

[[nodiscard]] std::uint32_t field_texture_index(std::uint32_t cascade, std::uint32_t field) {
    return cascade * cubey::render::kOceanSpectrumFieldCount + field;
}

[[nodiscard]] std::uint32_t ocean_enabled_cascade_mask(const render::OceanSurfaceConfig& config) {
    std::uint32_t mask = 0U;
    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (render::ocean_cascade_enabled(config, cascade)) {
            mask |= 1U << cascade;
        }
    }
    return mask;
}

[[nodiscard]] bool ocean_should_update_cascade(const render::OceanSurfaceConfig& config,
                                               std::uint32_t cascade, std::uint64_t frame_index,
                                               bool first_update) {
    if (!render::ocean_cascade_enabled(config, cascade)) {
        return false;
    }
    if (first_update) {
        return true;
    }
    const std::uint32_t interval = render::ocean_cascade_update_interval(config, cascade);
    return interval <= 1U || (frame_index % interval) == 0U;
}

[[nodiscard]] std::uint32_t log2_exact(std::uint32_t value) {
    if (!render::ocean_is_power_of_two(value)) {
        throw std::runtime_error("ocean FFT resolution must be a power of two");
    }
    std::uint32_t result = 0U;
    while (value > 1U) {
        value >>= 1U;
        ++result;
    }
    return result;
}

[[nodiscard]] render::ComputeDispatchGroups
ocean_dispatch_groups(const render::OceanSurfaceConfig& config, std::uint32_t cascade) {
    const std::uint32_t map_size = render::ocean_cascade_map_size(config, cascade);
    return render::ceil_dispatch_groups(map_size, map_size, 16U);
}

[[nodiscard]] std::string ocean_gpu_timing_label(const char* phase, std::uint32_t cascade) {
    return std::string("ocean.") + phase + ".c" + std::to_string(cascade);
}

[[nodiscard]] float ocean_jonswap_alpha(float wind_speed, float fetch_length_m) {
    constexpr float gravity = 9.81F;
    return 0.076F *
           std::pow((wind_speed * wind_speed) / std::max(fetch_length_m * gravity, 0.001F), 0.22F);
}

[[nodiscard]] float ocean_jonswap_peak_frequency(float wind_speed, float fetch_length_m) {
    constexpr float gravity = 9.81F;
    return 22.0F * std::pow((gravity * gravity) / std::max(wind_speed * fetch_length_m, 0.001F),
                            1.0F / 3.0F);
}

[[nodiscard]] float ocean_patch_wave_cull_margin_m(const render::OceanSurfaceConfig& config) {
    float scale_sum = 0.0F;
    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (render::ocean_cascade_enabled(config, cascade)) {
            scale_sum += render::ocean_cascade(config, cascade).displacement_scale;
        }
    }
    return std::max(96.0F, scale_sum * 24.0F * std::max(config.surface_shape_strength, 0.0F));
}

[[nodiscard]] Bounds3D ocean_mesh_patch_world_bounds(const render::OceanSurfaceConfig& config,
                                                     const render::OceanSurfaceFrame& surface_frame,
                                                     const render::OceanMeshPatch& patch,
                                                     math::Vec3 camera_position_m) {
    const float snap = render::ocean_mesh_patch_snap_size(patch);
    const float snapped_x = std::floor(camera_position_m.x / snap) * snap;
    const float snapped_z = std::floor(camera_position_m.z / snap) * snap;
    const float horizontal_margin = ocean_patch_wave_cull_margin_m(config);
    const float vertical_margin = horizontal_margin * 1.5F;
    const float min_x = snapped_x + patch.bounds.min_x - horizontal_margin;
    const float max_x = snapped_x + patch.bounds.max_x + horizontal_margin;
    const float min_z = snapped_z + patch.bounds.min_z - horizontal_margin;
    const float max_z = snapped_z + patch.bounds.max_z + horizontal_margin;

    float min_drop = 0.0F;
    const std::array<math::Vec2, 4> corners{
        math::Vec2{min_x, min_z},
        math::Vec2{min_x, max_z},
        math::Vec2{max_x, min_z},
        math::Vec2{max_x, max_z},
    };
    for (const math::Vec2 corner : corners) {
        const float distance =
            glm::length(math::Vec2{corner.x - camera_position_m.x, corner.y - camera_position_m.z});
        min_drop =
            std::min(min_drop, render::ocean_surface_curvature_drop_m(
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

[[nodiscard]] VkFormat ocean_field_format(OceanFieldPrecision precision) {
    switch (precision) {
    case OceanFieldPrecision::Full:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case OceanFieldPrecision::Half:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

[[nodiscard]] const char* ocean_precision_shader_suffix(OceanFieldPrecision precision) {
    switch (precision) {
    case OceanFieldPrecision::Full:
        return "";
    case OceanFieldPrecision::Half:
        return "_half";
    }
    return "";
}

[[nodiscard]] std::filesystem::path
ocean_compute_shader_path(const std::filesystem::path& shader_dir, const char* stem,
                          OceanFieldPrecision precision) {
    return shader_dir /
           (std::string(stem) + ocean_precision_shader_suffix(precision) + ".comp.spv");
}

void validate_ocean_field_format_support(const cubey::vulkan::Device& device,
                                         OceanFieldPrecision precision) {
    if (precision != OceanFieldPrecision::Half) {
        return;
    }
    constexpr VkFormatFeatureFlags kRequiredFormatFeatures =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (!device.supports_shader_storage_image_extended_formats() ||
        !device.supports_image_format_features(VK_FORMAT_R16G16B16A16_SFLOAT,
                                               kRequiredFormatFeatures)) {
        throw std::runtime_error(
            "ocean half field precision requires sampled rgba16f storage image support");
    }
}

[[nodiscard]] cubey::render::Texture2D make_ocean_field_texture(const cubey::vulkan::Device& device,
                                                                std::uint32_t resolution,
                                                                VkFormat format, bool sampled) {
    return cubey::render::Texture2D(device,
                                    cubey::render::Texture2DConfig{
                                        .extent = {resolution, resolution},
                                        .format = format,
                                        .usage = cubey::render::Texture2DUsage::StorageSampled,
                                        .create_sampler = sampled,
                                        .sampler =
                                            {
                                                .min_filter = VK_FILTER_LINEAR,
                                                .mag_filter = VK_FILTER_LINEAR,
                                                .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                            },
                                    });
}

[[nodiscard]] cubey::render::MaterialPassInfo ocean_surface_pass_info() {
    const VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(float) * 64U,
    };
    return {
        .label = "ocean.surface",
        .push_constants = {push_constant_range},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
}

[[nodiscard]] VkPushConstantRange compute_push_constant_range(std::uint32_t float_count) {
    return {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = static_cast<std::uint32_t>(sizeof(float) * float_count),
    };
}

[[nodiscard]] cubey::vulkan::DescriptorSetInfo
descriptor_info(std::span<const cubey::vulkan::DescriptorSetBindingConfig> bindings,
                std::uint32_t max_sets) {
    return cubey::vulkan::DescriptorSetInfo(bindings, max_sets);
}

[[nodiscard]] OceanSurfacePushConstants ocean_surface_push_constants(
    const render::OceanSurfaceConfig& config, const OceanSurfaceRuntimeFrameInfo& frame,
    const render::OceanSurfaceFrame& surface_frame, const render::OceanMeshPatch& patch,
    const render::OceanPatchShadingPlan& shading_plan) {
    const float debug_z = frame.debug_view == render::OceanRenderView::Exposure
                              ? frame.exposure
                              : static_cast<float>(surface_frame.mesh_config.mesh_lod_levels - 1U);
    return {
        .view_projection = frame.view_projection,
        .camera_time =
            {
                frame.camera_position_m.x,
                frame.camera_position_m.y,
                frame.camera_position_m.z,
                static_cast<float>(frame.elapsed_seconds),
            },
        .mesh_options =
            {
                static_cast<float>(patch.cells_x),
                static_cast<float>(patch.cells_z),
                surface_frame.mesh_config.mesh_extent,
                config.horizon_fog,
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
                static_cast<float>(static_cast<std::uint32_t>(frame.debug_view)),
                static_cast<float>(patch.level),
                debug_z,
                frame.wire_overlay ? std::clamp(frame.wire_opacity, 0.0F, 1.0F) : 0.0F,
            },
        .inspection_options =
            {
                static_cast<float>(frame.selected_cascade),
                config.shape_anti_repeat_strength,
                config.foam_density,
                config.foam_sharpness,
            },
        .tile_lengths =
            {
                config.cascades[0].tile_length,
                config.cascades[1].tile_length,
                config.cascades[2].tile_length,
                config.cascades[3].tile_length,
            },
        .displacement_scales =
            {
                config.cascades[0].displacement_scale,
                config.cascades[1].displacement_scale,
                config.cascades[2].displacement_scale,
                config.cascades[3].displacement_scale,
            },
        .normal_scales =
            {
                config.cascades[0].normal_scale,
                config.cascades[1].normal_scale,
                config.cascades[2].normal_scale,
                config.cascades[3].normal_scale,
            },
        .cascade4_options =
            {
                config.cascades[4].tile_length,
                config.cascades[4].displacement_scale,
                config.cascades[4].normal_scale,
                static_cast<float>(render::ocean_cascade_map_size(config, 4U)),
            },
        .water_color = {config.water_color_r, config.water_color_g, config.water_color_b,
                        config.roughness},
        .foam_color = {config.foam_color_r, config.foam_color_g, config.foam_color_b,
                       config.normal_strength},
        .quality_options =
            {
                static_cast<float>(shading_plan.self_shadow_steps),
                shading_plan.conservative_footprint_m,
                static_cast<float>(shading_plan.detail_filter),
                shading_plan.self_shadow_reduced ? 1.0F : 0.0F,
            },
    };
}

[[nodiscard]] OceanSurfaceFeatureUniforms
ocean_surface_feature_uniforms(const render::OceanSurfaceConfig& config,
                               const OceanSurfaceRuntimeFrameInfo& frame,
                               const render::OceanSurfaceFrame& surface_frame) {
    const bool cloud_reflection_valid = frame.cloud_environment_valid;
    return {
        .feature_options =
            {
                config.surface_shape_strength,
                config.surface_foam_strength,
                config.foam_history_strength,
                config.detail_anti_repeat_strength,
            },
        .feature_options2 =
            {
                config.terrain_foam_strength,
                config.shape_fade_distance_scale,
                config.normal_fade_distance_scale,
                config.foam_fade_distance_scale,
            },
        .fade_options =
            {
                static_cast<float>(config.detail_filter),
                0.0F,
                0.72F,
                static_cast<float>(render::ocean_cascade_map_size(config, 3U)),
            },
        .cascade_options =
            {
                static_cast<float>(ocean_enabled_cascade_mask(config)),
                static_cast<float>(render::ocean_cascade_map_size(config, 0U)),
                static_cast<float>(render::ocean_cascade_map_size(config, 1U)),
                static_cast<float>(render::ocean_cascade_map_size(config, 2U)),
            },
        .self_shadow_options =
            {
                config.self_shadow_strength,
                config.self_shadow_distance,
                config.self_shadow_bias,
                static_cast<float>(config.self_shadow_steps),
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
                surface_frame.surface_mode == render::OceanSurfaceMode::CurvedFar ? 1.0F : 0.0F,
                surface_frame.curvature_start_m,
                surface_frame.curvature_end_m,
                surface_frame.curvature_strength,
            },
        .far_field_options =
            {
                config.far_field_enabled ? 1.0F : 0.0F,
                config.far_field_start_m,
                config.far_field_end_m,
                0.0F,
            },
        .far_field_options2 =
            {
                config.far_roughness_strength,
                config.far_glint_strength,
                0.0F,
                0.0F,
            },
        .far_detail_options =
            {
                config.far_detail_footprint_start_m,
                config.far_detail_footprint_end_m,
                config.far_reflection_variation_strength,
                config.sun_glitter_width,
            },
        .cloud_shadow_world_to_uv_x = {},
        .cloud_shadow_world_to_uv_y = {},
        .cloud_lighting_options =
            {
                0.0F,
                0.0F,
                0.0F,
                cloud_reflection_valid ? config.cloud_reflection_strength : 0.0F,
            },
        .atmosphere_environment_options =
            {
                frame.atmosphere_environment_blend,
                0.0F,
                0.0F,
                0.0F,
            },
        .cloud_environment_options =
            {
                static_cast<float>(static_cast<std::uint32_t>(
                    render::OceanCloudReflectionSource::CachedEnvironment)),
                cloud_reflection_valid ? frame.cloud_environment_blend : 1.0F,
                cloud_reflection_valid ? 1.0F : 0.0F,
                4.0F,
            },
        .cloud_planar_right_aspect = {},
        .cloud_planar_up_tan_half_fovy = {},
        .cloud_planar_forward_lod = {},
        .cloud_planar_options = {},
        .sun_light_direction_intensity =
            {
                frame.lighting.sun_direction.x,
                frame.lighting.sun_direction.y,
                frame.lighting.sun_direction.z,
                frame.lighting.sun_intensity,
            },
        .sun_light_color =
            {
                frame.lighting.sun_color.x,
                frame.lighting.sun_color.y,
                frame.lighting.sun_color.z,
                0.0F,
            },
        .moon_light_direction_intensity =
            {
                frame.lighting.moon_direction.x,
                frame.lighting.moon_direction.y,
                frame.lighting.moon_direction.z,
                frame.lighting.moon_intensity,
            },
        .moon_light_color =
            {
                frame.lighting.moon_color.x,
                frame.lighting.moon_color.y,
                frame.lighting.moon_color.z,
                0.0F,
            },
    };
}

[[nodiscard]] OceanSpectrumPushConstants
ocean_spectrum_push_constants(const render::OceanSurfaceConfig& config,
                              std::uint32_t cascade_index) {
    const render::OceanCascadeConfig& cascade = render::ocean_cascade(config, cascade_index);
    const render::OceanCascadeDomain domain =
        config.spectral_domains_enabled ? render::ocean_cascade_domain(config, cascade_index)
                                        : render::OceanCascadeDomain{};
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
                cascade.wind_direction_degrees * render::kOceanPi / 180.0F,
            },
        .shape_options =
            {
                config.depth,
                cascade.swell,
                cascade.detail,
                cascade.spread,
            },
        .cascade_options =
            {
                static_cast<float>(cascade_index),
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                domain.active ? domain.low_k : 0.0F,
                domain.active ? domain.high_k : 0.0F,
            },
    };
}

[[nodiscard]] OceanModulatePushConstants
ocean_modulate_push_constants(const render::OceanSurfaceConfig& config, std::uint32_t cascade_index,
                              double elapsed_seconds) {
    const render::OceanCascadeConfig& cascade = render::ocean_cascade(config, cascade_index);
    return {
        .tile_depth_time =
            {
                cascade.tile_length,
                cascade.tile_length,
                config.depth,
                static_cast<float>(elapsed_seconds) + cascade.time_offset,
            },
        .cascade_options =
            {
                static_cast<float>(cascade_index),
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                0.0F,
                0.0F,
            },
    };
}

[[nodiscard]] OceanFftPushConstants
ocean_fft_push_constants(const render::OceanSurfaceConfig& config, std::uint32_t cascade_index,
                         std::uint32_t stage, bool horizontal, bool first_pass) {
    return {
        .fft_options =
            {
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                static_cast<float>(stage),
                horizontal ? 1.0F : 0.0F,
                first_pass ? 1.0F : 0.0F,
            },
        .pass_options = {},
    };
}

[[nodiscard]] OceanUnpackPushConstants
ocean_unpack_push_constants(const render::OceanSurfaceConfig& config, std::uint32_t cascade_index,
                            double delta_seconds, bool foam_initialized) {
    const render::OceanCascadeConfig& cascade = render::ocean_cascade(config, cascade_index);
    const float delta = static_cast<float>(delta_seconds > 0.0 ? delta_seconds : (1.0 / 60.0));
    return {
        .foam_options =
            {
                cascade.whitecap,
                delta * cascade.foam_amount * 7.5F,
                delta * std::max(0.5F, 10.0F - cascade.foam_amount) * 1.15F,
                foam_initialized ? 1.0F : 0.0F,
            },
        .cascade_options =
            {
                static_cast<float>(cascade_index),
                static_cast<float>(render::ocean_cascade_map_size(config, cascade_index)),
                0.0F,
                0.0F,
            },
    };
}

} // namespace

render::OceanSurfaceConfig
ocean_surface_config_from_options(const OceanSurfaceOptions& options) {
    render::OceanSurfaceConfig ocean;
    if (options.sea_state) {
        render::apply_ocean_sea_state(ocean, render::ocean_sea_state_from_name(*options.sea_state));
    }
    if (options.map_size) {
        ocean.map_size = *options.map_size;
    }
    if (options.mesh_cells) {
        ocean.mesh_cells = *options.mesh_cells;
    }
    if (options.mesh_lod_levels) {
        ocean.mesh_lod_levels = *options.mesh_lod_levels;
    }
    if (options.horizon_target_near_cell_m) {
        ocean.horizon_target_near_cell_m = *options.horizon_target_near_cell_m;
    }
    if (options.surface_shading_policy) {
        ocean.surface_shading_policy =
            render::ocean_surface_shading_policy_from_name(*options.surface_shading_policy);
    }
    if (options.self_shadow_strength) {
        ocean.self_shadow_strength = *options.self_shadow_strength;
    }
    if (options.self_shadow_steps) {
        ocean.self_shadow_steps = *options.self_shadow_steps;
    }
    if (options.self_shadow_far_steps) {
        ocean.self_shadow_far_steps = *options.self_shadow_far_steps;
    }
    if (options.shape_anti_repeat_strength) {
        ocean.shape_anti_repeat_strength = *options.shape_anti_repeat_strength;
    }
    if (options.detail_anti_repeat_strength) {
        ocean.detail_anti_repeat_strength = *options.detail_anti_repeat_strength;
    }
    if (options.detail_filter) {
        ocean.detail_filter = render::ocean_detail_filter_from_name(*options.detail_filter);
    }
    if (options.spectral_domains) {
        ocean.spectral_domains_enabled = *options.spectral_domains;
    }
    if (options.terrain_fields) {
        ocean.terrain_fields_enabled = *options.terrain_fields;
    }
    if (options.field_precision) {
        ocean.field_precision = render::ocean_field_precision_from_name(*options.field_precision);
    }
    if (options.surface_mode) {
        ocean.surface_mode = render::ocean_surface_mode_from_name(*options.surface_mode);
    }
    if (options.planet_radius_scale) {
        ocean.planet_radius_scale = *options.planet_radius_scale;
    }
    if (options.curvature_start_ratio) {
        ocean.curvature_start_ratio = *options.curvature_start_ratio;
    }
    if (options.curvature_end_ratio) {
        ocean.curvature_end_ratio = *options.curvature_end_ratio;
    }
    if (options.curvature_strength) {
        ocean.curvature_strength = *options.curvature_strength;
    }
    if (options.cloud_reflection_strength) {
        ocean.cloud_reflection_strength = *options.cloud_reflection_strength;
    }
    if (options.cloud_reflection_source) {
        ocean.cloud_reflection_source =
            render::ocean_cloud_reflection_source_from_name(*options.cloud_reflection_source);
    }
    if (options.cloud_environment_extent) {
        ocean.cloud_environment_extent = *options.cloud_environment_extent;
    }
    if (options.cloud_environment_update_hz) {
        ocean.cloud_environment_update_hz = *options.cloud_environment_update_hz;
    }
    if (options.cloud_planar_resolution_scale) {
        ocean.cloud_planar_resolution_scale = *options.cloud_planar_resolution_scale;
    }
    if (options.cloud_planar_view_steps) {
        ocean.cloud_planar_view_steps = *options.cloud_planar_view_steps;
    }
    if (options.cloud_planar_guard_band) {
        ocean.cloud_planar_guard_band = *options.cloud_planar_guard_band;
    }
    if (options.cloud_shadow_strength) {
        ocean.cloud_shadow_strength = *options.cloud_shadow_strength;
    }
    render::validate_ocean_config(ocean);
    return ocean;
}

void OceanSurfaceRuntime::create(const cubey::vulkan::Device& device,
                                 const OceanSurfaceRuntimeCreateInfo& config) {
    reset();
    render::validate_ocean_config(config.ocean);
    if (config.shader_dir.empty()) {
        throw std::runtime_error("ocean GPU resources require a shader directory");
    }
    if (config.color_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("ocean surface pipeline requires a color format");
    }
    if (config.depth_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("ocean surface pipeline requires a depth format");
    }
    if (config.target_extent.width == 0U || config.target_extent.height == 0U) {
        throw std::runtime_error("ocean surface pipeline requires a target extent");
    }
    if (config.frame_slot_count == 0U) {
        throw std::runtime_error("ocean surface pipeline requires at least one frame slot");
    }

    ocean_config_ = config.ocean;
    resolution_ = config.ocean.map_size;
    validate_ocean_field_format_support(device, config.ocean.field_precision);
    create_textures(device, config.ocean);
    surface_feature_uniforms_.emplace(device, config.frame_slot_count);
    fallback_terrain_uniforms_.emplace(device, config.frame_slot_count);
    create_descriptor_sets(device, config.frame_slot_count);
    update_descriptors(device);
    create_pipelines(device, config);
    profiler_.emplace(device, config.frame_slot_count, kOceanGpuProfilerPassCapacity);
}

void OceanSurfaceRuntime::reset() {
    profiler_.reset();
    for (auto& pipeline : surface_pipelines_) {
        pipeline.reset();
    }
    unpack_pipeline_.reset();
    fft_pipeline_.reset();
    modulate_pipeline_.reset();
    spectrum_pipeline_.reset();

    surface_pool_.reset();
    surface_layout_.reset();
    surface_feature_uniforms_.reset();
    fallback_terrain_uniforms_.reset();
    unpack_pool_.reset();
    unpack_layout_.reset();
    fft_pool_.reset();
    fft_layout_.reset();
    modulate_pool_.reset();
    modulate_layout_.reset();
    spectrum_pool_.reset();
    spectrum_layout_.reset();

    surface_sets_.clear();
    unpack_sets_ = {};
    fft_sets_ = {};
    modulate_sets_ = {};
    spectrum_sets_ = {};

    foam_ = {};
    normal_ = {};
    displacement_ = {};
    pong_ = {};
    ping_ = {};
    fields_ = {};
    h0_ = {};
    fallback_field_.reset();
    cascade_allocated_ = {};
    cascade_resolutions_ = {};
    resolution_ = 0;
    ocean_config_ = {};
    frame_ = {};
    draw_plan_ = {};
    patches_ = {};
    visible_patches_ = {};
    shading_plans_ = {};
    frame_prepared_ = false;
    textures_initialized_ = false;
    spectrum_initialized_ = false;
    foam_initialized_ = false;
    compute_frame_index_ = 0U;
}

void OceanSurfaceRuntime::set_config(const render::OceanSurfaceConfig& config) {
    render::validate_ocean_config(config);
    if (!initialized()) {
        throw std::runtime_error("ocean surface runtime is not initialized");
    }
    if (config.map_size != ocean_config_.map_size ||
        config.field_precision != ocean_config_.field_precision ||
        config.cascade_enabled != ocean_config_.cascade_enabled ||
        config.cascade_map_sizes != ocean_config_.cascade_map_sizes) {
        throw std::runtime_error(
            "ocean surface resource layout changes require runtime recreation");
    }
    const bool wave_source_changed =
        config.depth != ocean_config_.depth ||
        config.spectral_domains_enabled != ocean_config_.spectral_domains_enabled ||
        config.cascades != ocean_config_.cascades;
    ocean_config_ = config;
    if (wave_source_changed) {
        reset_simulation();
    }
}

void OceanSurfaceRuntime::reset_simulation() {
    spectrum_initialized_ = false;
    foam_initialized_ = false;
    compute_frame_index_ = 0U;
}

void OceanSurfaceRuntime::prepare_frame(render::FrameSlot frame_slot,
                                        const OceanSurfaceRuntimeFrameInfo& frame) {
    if (!initialized()) {
        throw std::runtime_error("ocean surface runtime is not initialized");
    }
    render::validate_frame_slot(frame_slot);
    if (frame.viewport_extent.width == 0U || frame.viewport_extent.height == 0U ||
        !std::isfinite(frame.vertical_fov_radians) || frame.vertical_fov_radians <= 0.0F ||
        frame.vertical_fov_radians >= render::kOceanPi || !std::isfinite(frame.planet_radius_m) ||
        frame.planet_radius_m <= 0.0F || !std::isfinite(frame.water_datum_m) ||
        !std::isfinite(frame.elapsed_seconds) || !std::isfinite(frame.delta_seconds) ||
        frame.delta_seconds < 0.0) {
        throw std::runtime_error("ocean surface frame inputs are invalid");
    }

    frame_ = frame;
    prepared_frame_slot_ = frame_slot;
    draw_plan_ = {};
    draw_plan_.surface_frame = render::ocean_surface_frame_from_camera(
        ocean_config_, frame.camera_position_m, frame.planet_radius_m, frame.water_datum_m);
    patches_ = render::ocean_mesh_clipmap_patches(draw_plan_.surface_frame.mesh_config);
    visible_patches_ = {};
    shading_plans_ = {};

    const scene::Frustum3D frustum = scene::frustum_from_view_projection(frame.view_projection);
    draw_plan_.stats.generated_patches = static_cast<std::uint32_t>(patches_.count);
    draw_plan_.stats.generated_triangles =
        render::ocean_mesh_total_triangle_count(draw_plan_.surface_frame.mesh_config);
    for (std::size_t index = 0U; index < patches_.count; ++index) {
        const render::OceanMeshPatch& patch = patches_.patches[index];
        const render::OceanPatchShadingPlan shading_plan = render::ocean_patch_shading_plan(
            ocean_config_, patch, frame.camera_position_m.x, frame.camera_position_m.z,
            draw_plan_.surface_frame.horizon.camera_altitude_m, frame.vertical_fov_radians,
            frame.viewport_extent.height);
        shading_plans_[index] = shading_plan;
        visible_patches_[index] = scene::intersects(
            frustum, ocean_mesh_patch_world_bounds(ocean_config_, draw_plan_.surface_frame, patch,
                                                   frame.camera_position_m));
        if (!visible_patches_[index]) {
            continue;
        }
        ++draw_plan_.stats.submitted_patches;
        const std::uint32_t triangles = render::ocean_mesh_patch_triangle_count(patch);
        draw_plan_.stats.submitted_triangles += triangles;
        if (shading_plan.detail_filter_reduced) {
            ++draw_plan_.stats.reduced_filter_patches;
            draw_plan_.stats.reduced_filter_triangles += triangles;
        }
        if (shading_plan.self_shadow_reduced) {
            ++draw_plan_.stats.reduced_shadow_patches;
            draw_plan_.stats.reduced_shadow_triangles += triangles;
        }
    }
    draw_plan_.stats.culled_patches =
        draw_plan_.stats.generated_patches - draw_plan_.stats.submitted_patches;
    upload_surface_feature_uniforms(
        frame_slot,
        ocean_surface_feature_uniforms(ocean_config_, frame_, draw_plan_.surface_frame));
    frame_prepared_ = true;
}

void OceanSurfaceRuntime::record_update(const vulkan::CommandRecorder& recorder,
                                        render::FrameSlot frame_slot) {
    if (!frame_prepared_ || frame_slot.index != prepared_frame_slot_.index ||
        frame_slot.count != prepared_frame_slot_.count) {
        throw std::runtime_error("ocean surface runtime frame was not prepared");
    }
    if (profiler_.has_value()) {
        profiler_->begin_frame(recorder.handle(), frame_slot.index);
    }
    if (!textures_initialized_) {
        recorder.transition_image_layout(
            vulkan::begin_storage_image_write_transition(fallback_field().handle()));
        for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
            if (!cascade_allocated(cascade)) {
                continue;
            }
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(h0(cascade).handle()));
            for (std::uint32_t field_index = 0U; field_index < render::kOceanSpectrumFieldCount;
                 ++field_index) {
                recorder.transition_image_layout(vulkan::begin_storage_image_write_transition(
                    field(cascade, field_index).handle()));
                recorder.transition_image_layout(vulkan::begin_storage_image_write_transition(
                    ping(cascade, field_index).handle()));
                recorder.transition_image_layout(vulkan::begin_storage_image_write_transition(
                    pong(cascade, field_index).handle()));
            }
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(displacement(cascade).handle()));
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(normal(cascade).handle()));
            recorder.transition_image_layout(
                vulkan::begin_storage_image_write_transition(foam(cascade).handle()));
        }
        textures_initialized_ = true;
    }

    if (!spectrum_initialized_) {
        for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
            if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                             !foam_initialized_)) {
                continue;
            }
            vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                            ocean_gpu_timing_label("spectrum", cascade));
            render::record_compute_pipeline_dispatch(
                recorder,
                render::compute_pipeline_dispatch_info(
                    spectrum_pipeline(), spectrum_set(cascade),
                    ocean_dispatch_groups(ocean_config_, cascade)),
                VK_SHADER_STAGE_COMPUTE_BIT, ocean_spectrum_push_constants(ocean_config_, cascade));
        }
        vulkan::record_compute_shader_write_barrier(recorder.handle());
        spectrum_initialized_ = true;
    }

    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                        ocean_gpu_timing_label("modulate", cascade));
        render::record_compute_pipeline_dispatch(
            recorder,
            render::compute_pipeline_dispatch_info(modulate_pipeline(), modulate_set(cascade),
                                                   ocean_dispatch_groups(ocean_config_, cascade)),
            VK_SHADER_STAGE_COMPUTE_BIT,
            ocean_modulate_push_constants(ocean_config_, cascade, frame_.elapsed_seconds));
    }
    vulkan::record_compute_shader_write_barrier(recorder.handle());

    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                        ocean_gpu_timing_label("fft", cascade));
        const render::ComputeDispatchGroups groups = ocean_dispatch_groups(ocean_config_, cascade);
        const std::uint32_t stage_count =
            log2_exact(render::ocean_cascade_map_size(ocean_config_, cascade));
        for (std::uint32_t field_index = 0U; field_index < render::kOceanSpectrumFieldCount;
             ++field_index) {
            bool source_is_ping = true;
            for (std::uint32_t stage = 1U; stage <= stage_count; ++stage) {
                const std::uint32_t set_index = stage == 1U ? 0U : (source_is_ping ? 1U : 2U);
                render::record_compute_pipeline_dispatch(
                    recorder,
                    render::compute_pipeline_dispatch_info(
                        fft_pipeline(), fft_set(cascade, field_index, set_index), groups),
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    ocean_fft_push_constants(ocean_config_, cascade, stage, true, stage == 1U));
                vulkan::record_compute_shader_write_barrier(recorder.handle());
                source_is_ping = stage == 1U ? true : !source_is_ping;
            }
            for (std::uint32_t stage = 1U; stage <= stage_count; ++stage) {
                const std::uint32_t set_index = source_is_ping ? 1U : 2U;
                render::record_compute_pipeline_dispatch(
                    recorder,
                    render::compute_pipeline_dispatch_info(
                        fft_pipeline(), fft_set(cascade, field_index, set_index), groups),
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    ocean_fft_push_constants(ocean_config_, cascade, stage, false, stage == 1U));
                vulkan::record_compute_shader_write_barrier(recorder.handle());
                source_is_ping = !source_is_ping;
            }
        }
    }

    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (!ocean_should_update_cascade(ocean_config_, cascade, compute_frame_index_,
                                         !foam_initialized_)) {
            continue;
        }
        vulkan::GpuTimestampScope scope(profiler(), recorder.handle(), frame_slot.index,
                                        ocean_gpu_timing_label("unpack", cascade));
        render::record_compute_pipeline_dispatch(
            recorder,
            render::compute_pipeline_dispatch_info(unpack_pipeline(), unpack_set(cascade),
                                                   ocean_dispatch_groups(ocean_config_, cascade)),
            VK_SHADER_STAGE_COMPUTE_BIT,
            ocean_unpack_push_constants(ocean_config_, cascade, frame_.delta_seconds,
                                        foam_initialized_));
    }
    vulkan::record_shader_write_barrier(recorder.handle(),
                                        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_SHADER_READ_BIT);
    foam_initialized_ = true;
    ++compute_frame_index_;
}

void OceanSurfaceRuntime::record_surface_draws(const vulkan::CommandRecorder& recorder,
                                               render::FrameSlot frame_slot) const {
    if (!frame_prepared_ || frame_slot.index != prepared_frame_slot_.index ||
        frame_slot.count != prepared_frame_slot_.count) {
        throw std::runtime_error("ocean surface runtime frame was not prepared");
    }
    const render::GraphicsPipelineResource* bound_pipeline = nullptr;
    for (std::size_t index = 0U; index < patches_.count; ++index) {
        if (!visible_patches_[index]) {
            continue;
        }
        const render::OceanMeshPatch& patch = patches_.patches[index];
        const render::OceanPatchShadingPlan& shading_plan = shading_plans_[index];
        const render::GraphicsPipelineResource& pipeline =
            surface_pipeline(shading_plan.detail_filter);
        if (bound_pipeline != &pipeline) {
            recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
            recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0U,
                                         surface_set(frame_slot));
            bound_pipeline = &pipeline;
        }
        recorder.push_constants(
            pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U,
            ocean_surface_push_constants(ocean_config_, frame_, draw_plan_.surface_frame, patch,
                                         shading_plan));
        recorder.draw(render::ocean_mesh_patch_vertex_count(patch));
    }
}

void OceanSurfaceRuntime::create_textures(const cubey::vulkan::Device& device,
                                          const cubey::render::OceanSurfaceConfig& config) {
    const VkFormat field_format = ocean_field_format(config.field_precision);
    fallback_field_.emplace(make_ocean_field_texture(device, 1U, field_format, true));
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        cascade_allocated_[cascade] = cubey::render::ocean_cascade_enabled(config, cascade);
        cascade_resolutions_[cascade] = cubey::render::ocean_cascade_map_size(config, cascade);
        if (!cascade_allocated_[cascade]) {
            continue;
        }
        const std::uint32_t map_size = cascade_resolutions_[cascade];
        h0_[cascade].emplace(make_ocean_field_texture(device, map_size, field_format, false));
        for (std::uint32_t field = 0; field < cubey::render::kOceanSpectrumFieldCount; ++field) {
            const std::uint32_t index = field_texture_index(cascade, field);
            fields_[index].emplace(make_ocean_field_texture(device, map_size, field_format, false));
            ping_[index].emplace(make_ocean_field_texture(device, map_size, field_format, false));
            pong_[index].emplace(make_ocean_field_texture(device, map_size, field_format, false));
        }
        displacement_[cascade].emplace(
            make_ocean_field_texture(device, map_size, field_format, true));
        normal_[cascade].emplace(make_ocean_field_texture(device, map_size, field_format, true));
        foam_[cascade].emplace(make_ocean_field_texture(device, map_size, field_format, true));
    }
}

void OceanSurfaceRuntime::create_descriptor_sets(const cubey::vulkan::Device& device,
                                                 std::uint32_t frame_slot_count) {
    const std::array spectrum_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo spectrum_info =
        descriptor_info(spectrum_bindings, cubey::render::kOceanCascadeCount);
    spectrum_layout_.emplace(device, spectrum_info.layout_info());
    spectrum_pool_.emplace(device, spectrum_info.pool_info());
    for (VkDescriptorSet& set : spectrum_sets_) {
        set = spectrum_pool_->allocate(spectrum_layout_->handle());
    }

    const std::array modulate_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo modulate_info =
        descriptor_info(modulate_bindings, cubey::render::kOceanCascadeCount);
    modulate_layout_.emplace(device, modulate_info.layout_info());
    modulate_pool_.emplace(device, modulate_info.pool_info());
    for (VkDescriptorSet& set : modulate_sets_) {
        set = modulate_pool_->allocate(modulate_layout_->handle());
    }

    const std::array fft_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo fft_info =
        descriptor_info(fft_bindings, static_cast<std::uint32_t>(fft_sets_.size()));
    fft_layout_.emplace(device, fft_info.layout_info());
    fft_pool_.emplace(device, fft_info.pool_info());
    for (VkDescriptorSet& set : fft_sets_) {
        set = fft_pool_->allocate(fft_layout_->handle());
    }

    const std::array unpack_bindings{
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 2,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 3,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = 4,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const cubey::vulkan::DescriptorSetInfo unpack_info =
        descriptor_info(unpack_bindings, cubey::render::kOceanCascadeCount);
    unpack_layout_.emplace(device, unpack_info.layout_info());
    unpack_pool_.emplace(device, unpack_info.pool_info());
    for (VkDescriptorSet& set : unpack_sets_) {
        set = unpack_pool_->allocate(unpack_layout_->handle());
    }

    std::array<cubey::vulkan::DescriptorSetBindingConfig, kOceanSurfaceBindingCount>
        surface_bindings{};
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        surface_bindings[cascade] = cubey::vulkan::DescriptorSetBindingConfig{
            .binding = cascade,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        surface_bindings[cubey::render::kOceanCascadeCount + cascade] =
            cubey::vulkan::DescriptorSetBindingConfig{
                .binding = cubey::render::kOceanCascadeCount + cascade,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
    }
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        surface_bindings[cubey::render::kOceanCascadeCount * 2U + cascade] =
            cubey::vulkan::DescriptorSetBindingConfig{
                .binding = cubey::render::kOceanCascadeCount * 2U + cascade,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            };
    }
    surface_bindings[kOceanSurfaceReflectionBinding] = cubey::vulkan::DescriptorSetBindingConfig{
        .binding = kOceanSurfaceReflectionBinding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    surface_bindings[kOceanSurfaceSkyRadianceBinding] = cubey::vulkan::DescriptorSetBindingConfig{
        .binding = kOceanSurfaceSkyRadianceBinding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    surface_bindings[kOceanSurfaceTerrainFieldBinding] = cubey::vulkan::DescriptorSetBindingConfig{
        .binding = kOceanSurfaceTerrainFieldBinding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    surface_bindings[kOceanSurfaceTerrainFieldUniformBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceTerrainFieldUniformBinding,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceFeatureUniformBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceFeatureUniformBinding,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceCloudShadowBinding] = cubey::vulkan::DescriptorSetBindingConfig{
        .binding = kOceanSurfaceCloudShadowBinding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    surface_bindings[kOceanSurfaceCloudEnvironmentPreviousBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceCloudEnvironmentPreviousBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceCloudEnvironmentCurrentBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceCloudEnvironmentCurrentBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceCloudPlanarReflectionBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceCloudPlanarReflectionBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    surface_bindings[kOceanSurfaceReflectionCurrentBinding] =
        cubey::vulkan::DescriptorSetBindingConfig{
            .binding = kOceanSurfaceReflectionCurrentBinding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    const cubey::vulkan::DescriptorSetInfo surface_info =
        descriptor_info(surface_bindings, frame_slot_count);
    surface_layout_.emplace(device, surface_info.layout_info());
    surface_pool_.emplace(device, surface_info.pool_info());
    surface_sets_.resize(frame_slot_count, VK_NULL_HANDLE);
    for (VkDescriptorSet& set : surface_sets_) {
        set = surface_pool_->allocate(surface_layout_->handle());
    }
}

void OceanSurfaceRuntime::update_descriptors(const cubey::vulkan::Device& device) {
    cubey::vulkan::DescriptorWriteBatch writes;
    for (std::uint32_t cascade = 0; cascade < cubey::render::kOceanCascadeCount; ++cascade) {
        const cubey::render::Texture2D& displacement_texture =
            cascade_allocated(cascade) ? displacement(cascade) : fallback_field();
        const cubey::render::Texture2D& normal_texture =
            cascade_allocated(cascade) ? normal(cascade) : fallback_field();
        const cubey::render::Texture2D& foam_texture =
            cascade_allocated(cascade) ? foam(cascade) : fallback_field();

        for (VkDescriptorSet surface_set : surface_sets_) {
            writes.combined_image_sampler(surface_set, cascade,
                                          displacement_texture.sampler().handle(),
                                          displacement_texture.view(), VK_IMAGE_LAYOUT_GENERAL);
            writes.combined_image_sampler(surface_set, cascade + cubey::render::kOceanCascadeCount,
                                          normal_texture.sampler().handle(), normal_texture.view(),
                                          VK_IMAGE_LAYOUT_GENERAL);
            writes.combined_image_sampler(
                surface_set, cascade + cubey::render::kOceanCascadeCount * 2U,
                foam_texture.sampler().handle(), foam_texture.view(), VK_IMAGE_LAYOUT_GENERAL);
        }
        if (!cascade_allocated(cascade)) {
            continue;
        }

        writes.storage_image(spectrum_set(cascade), 0, h0(cascade).view())
            .storage_image(modulate_set(cascade), 0, h0(cascade).view())
            .storage_image(modulate_set(cascade), 1, field(cascade, 0).view())
            .storage_image(modulate_set(cascade), 2, field(cascade, 1).view())
            .storage_image(unpack_set(cascade), 0, pong(cascade, 0).view())
            .storage_image(unpack_set(cascade), 1, pong(cascade, 1).view())
            .storage_image(unpack_set(cascade), 2, displacement(cascade).view())
            .storage_image(unpack_set(cascade), 3, normal(cascade).view())
            .storage_image(unpack_set(cascade), 4, foam(cascade).view());

        for (std::uint32_t field_index = 0; field_index < cubey::render::kOceanSpectrumFieldCount;
             ++field_index) {
            const std::uint32_t base_fft_set =
                (cascade * cubey::render::kOceanSpectrumFieldCount + field_index) * 3U;
            writes
                .storage_image(fft_sets_[base_fft_set + 0U], 0, field(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 0U], 1, ping(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 1U], 0, ping(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 1U], 1, pong(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 2U], 0, pong(cascade, field_index).view())
                .storage_image(fft_sets_[base_fft_set + 2U], 1, ping(cascade, field_index).view());
        }
    }
    if (!surface_feature_uniforms_.has_value()) {
        throw std::runtime_error("ocean surface feature uniforms are not initialized");
    }
    if (!fallback_terrain_uniforms_.has_value()) {
        throw std::runtime_error("ocean fallback terrain uniforms are not initialized");
    }
    const std::uint32_t slot_count = surface_feature_uniforms_->slot_count();
    for (std::uint32_t index = 0; index < slot_count; ++index) {
        const cubey::render::FrameSlot frame_slot{.index = index, .count = slot_count};
        fallback_terrain_uniforms_->upload(frame_slot, {});
        writes
            .uniform_buffer(surface_set(frame_slot), kOceanSurfaceFeatureUniformBinding,
                            surface_feature_uniforms_->buffer(frame_slot).handle(),
                            surface_feature_uniforms_->range())
            .uniform_buffer(surface_set(frame_slot), kOceanSurfaceTerrainFieldUniformBinding,
                            fallback_terrain_uniforms_->buffer(frame_slot).handle(),
                            fallback_terrain_uniforms_->range())
            .combined_image_sampler(surface_set(frame_slot), kOceanSurfaceTerrainFieldBinding,
                                    fallback_field().sampler().handle(), fallback_field().view(),
                                    VK_IMAGE_LAYOUT_GENERAL)
            .combined_image_sampler(surface_set(frame_slot), kOceanSurfaceCloudShadowBinding,
                                    fallback_field().sampler().handle(), fallback_field().view(),
                                    VK_IMAGE_LAYOUT_GENERAL)
            .combined_image_sampler(surface_set(frame_slot),
                                    kOceanSurfaceCloudPlanarReflectionBinding,
                                    fallback_field().sampler().handle(), fallback_field().view(),
                                    VK_IMAGE_LAYOUT_GENERAL);
    }
    writes.update(device);
}

void OceanSurfaceRuntime::update_atmosphere_probe_descriptors(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    const cubey::render::TextureCube& previous, const cubey::render::TextureCube& current,
    const cubey::render::TextureCube& sky_radiance) {
    if (surface_sets_.empty()) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    cubey::vulkan::DescriptorWriteBatch writes;
    const VkDescriptorSet set = surface_set(frame_slot);
    writes
        .combined_image_sampler(set, kOceanSurfaceReflectionBinding, previous.sampler().handle(),
                                previous.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceReflectionCurrentBinding,
                                current.sampler().handle(), current.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceSkyRadianceBinding,
                                sky_radiance.sampler().handle(), sky_radiance.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceCloudEnvironmentPreviousBinding,
                                current.sampler().handle(), current.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, kOceanSurfaceCloudEnvironmentCurrentBinding,
                                current.sampler().handle(), current.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .update(device);
}

void OceanSurfaceRuntime::update_terrain_ocean_field_descriptor(
    const cubey::vulkan::Device& device, const cubey::render::Texture2D& fields) {
    if (surface_sets_.empty()) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    cubey::vulkan::DescriptorWriteBatch writes;
    for (VkDescriptorSet surface_set : surface_sets_) {
        writes.combined_image_sampler(surface_set, kOceanSurfaceTerrainFieldBinding,
                                      fields.sampler().handle(), fields.view(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    writes.update(device);
}

void OceanSurfaceRuntime::update_terrain_ocean_field_uniform_descriptor(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot, VkBuffer buffer,
    VkDeviceSize range) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .uniform_buffer(surface_set(frame_slot), kOceanSurfaceTerrainFieldUniformBinding, buffer,
                        range)
        .update(device);
}

void OceanSurfaceRuntime::update_cloud_shadow_descriptor(const cubey::vulkan::Device& device,
                                                         cubey::render::FrameSlot frame_slot,
                                                         VkSampler sampler, VkImageView image_view,
                                                         VkImageLayout image_layout) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(surface_set(frame_slot), kOceanSurfaceCloudShadowBinding, sampler,
                                image_view, image_layout)
        .update(device);
}

void OceanSurfaceRuntime::update_cloud_environment_descriptors(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    const cubey::render::TextureCube& previous, const cubey::render::TextureCube& current) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(
            surface_set(frame_slot), kOceanSurfaceCloudEnvironmentPreviousBinding,
            previous.sampler().handle(), previous.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(
            surface_set(frame_slot), kOceanSurfaceCloudEnvironmentCurrentBinding,
            current.sampler().handle(), current.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .update(device);
}

void OceanSurfaceRuntime::update_cloud_planar_reflection_descriptor(
    const cubey::vulkan::Device& device, cubey::render::FrameSlot frame_slot,
    const cubey::render::Texture2D& texture) {
    cubey::vulkan::DescriptorWriteBatch writes;
    writes
        .combined_image_sampler(surface_set(frame_slot), kOceanSurfaceCloudPlanarReflectionBinding,
                                texture.sampler().handle(), texture.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .update(device);
}

void OceanSurfaceRuntime::upload_surface_feature_uniforms(
    cubey::render::FrameSlot frame_slot, const OceanSurfaceFeatureUniforms& uniforms) const {
    if (!surface_feature_uniforms_.has_value()) {
        throw std::runtime_error("ocean surface feature uniforms are not initialized");
    }
    surface_feature_uniforms_->upload(frame_slot, uniforms);
}

void OceanSurfaceRuntime::create_pipelines(const cubey::vulkan::Device& device,
                                           const OceanSurfaceRuntimeCreateInfo& config) {
    const VkPushConstantRange spectrum_push_constants = compute_push_constant_range(16U);
    const VkPushConstantRange modulate_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange fft_push_constants = compute_push_constant_range(8U);
    const VkPushConstantRange unpack_push_constants = compute_push_constant_range(8U);

    const std::array spectrum_layouts{spectrum_layout_->handle()};
    spectrum_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(ocean_compute_shader_path(
                        config.shader_dir, "ocean_spectrum", config.ocean.field_precision)),
                    .descriptor_set_layouts = spectrum_layouts,
                    .push_constants = {&spectrum_push_constants, 1},
                });

    const std::array modulate_layouts{modulate_layout_->handle()};
    modulate_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(ocean_compute_shader_path(
                        config.shader_dir, "ocean_modulate", config.ocean.field_precision)),
                    .descriptor_set_layouts = modulate_layouts,
                    .push_constants = {&modulate_push_constants, 1},
                });

    const std::array fft_layouts{fft_layout_->handle()};
    fft_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(ocean_compute_shader_path(
                        config.shader_dir, "ocean_fft", config.ocean.field_precision)),
                    .descriptor_set_layouts = fft_layouts,
                    .push_constants = {&fft_push_constants, 1},
                });

    const std::array unpack_layouts{unpack_layout_->handle()};
    unpack_pipeline_.emplace(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(ocean_compute_shader_path(
                        config.shader_dir, "ocean_unpack", config.ocean.field_precision)),
                    .descriptor_set_layouts = unpack_layouts,
                    .push_constants = {&unpack_push_constants, 1},
                });

    const std::array surface_layouts{surface_layout_->handle()};
    for (std::size_t index = 0; index < surface_pipelines_.size(); ++index) {
        const std::filesystem::path fragment_path =
            index == 0U ? shader_path(config.shader_dir, "ocean.frag.spv")
                        : config.shader_dir / "filters" /
                              (index == 1U ? std::filesystem::path("bilinear")
                                           : std::filesystem::path("bicubic")) /
                              "ocean.frag.spv";
        const std::array surface_shader_stage_files{
            cubey::render::vertex_shader_file(shader_path(config.shader_dir, "ocean.vert.spv")),
            cubey::render::fragment_shader_file(fragment_path),
        };
        surface_pipelines_[index].emplace(device,
                                          cubey::render::GraphicsPipelineFileResourceConfig{
                                              .extent = config.target_extent,
                                              .color_format = config.color_format,
                                              .depth_format = config.depth_format,
                                              .shader_stage_files = surface_shader_stage_files,
                                              .descriptor_set_layouts = surface_layouts,
                                              .material_pass = ocean_surface_pass_info(),
                                          });
    }
}

const cubey::render::GraphicsPipelineResource&
OceanSurfaceRuntime::surface_pipeline(cubey::render::OceanDetailFilter filter) const {
    const std::size_t index = static_cast<std::size_t>(filter);
    if (index >= surface_pipelines_.size() || !surface_pipelines_[index].has_value()) {
        throw std::runtime_error("ocean surface pipeline is not initialized");
    }
    return surface_pipelines_[index].value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::spectrum_pipeline() const {
    if (!spectrum_pipeline_.has_value()) {
        throw std::runtime_error("ocean spectrum pipeline is not initialized");
    }
    return spectrum_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::modulate_pipeline() const {
    if (!modulate_pipeline_.has_value()) {
        throw std::runtime_error("ocean modulate pipeline is not initialized");
    }
    return modulate_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::fft_pipeline() const {
    if (!fft_pipeline_.has_value()) {
        throw std::runtime_error("ocean FFT pipeline is not initialized");
    }
    return fft_pipeline_.value();
}

const cubey::render::ComputePipelineResource& OceanSurfaceRuntime::unpack_pipeline() const {
    if (!unpack_pipeline_.has_value()) {
        throw std::runtime_error("ocean unpack pipeline is not initialized");
    }
    return unpack_pipeline_.value();
}

VkDescriptorSet OceanSurfaceRuntime::spectrum_set(std::uint32_t cascade) const {
    return descriptor_at(spectrum_sets_, cascade, "ocean spectrum descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::modulate_set(std::uint32_t cascade) const {
    return descriptor_at(modulate_sets_, cascade, "ocean modulate descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::fft_set(std::uint32_t cascade, std::uint32_t field,
                                             std::uint32_t set_index) const {
    if (field >= cubey::render::kOceanSpectrumFieldCount || set_index >= 3U) {
        throw std::runtime_error("ocean FFT descriptor index out of range");
    }
    return descriptor_at(
        fft_sets_, (cascade * cubey::render::kOceanSpectrumFieldCount + field) * 3U + set_index,
        "ocean FFT descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::unpack_set(std::uint32_t cascade) const {
    return descriptor_at(unpack_sets_, cascade, "ocean unpack descriptor set");
}

VkDescriptorSet OceanSurfaceRuntime::surface_set(cubey::render::FrameSlot frame_slot) const {
    cubey::render::validate_frame_slot(frame_slot);
    if (frame_slot.count != surface_sets_.size()) {
        throw std::runtime_error("ocean surface descriptor set frame slot count mismatch");
    }
    const VkDescriptorSet set = surface_sets_.at(static_cast<std::size_t>(frame_slot.index));
    if (set == VK_NULL_HANDLE) {
        throw std::runtime_error("ocean surface descriptor set is not initialized");
    }
    return set;
}

const cubey::render::Texture2D& OceanSurfaceRuntime::h0(std::uint32_t cascade) const {
    return texture_at(h0_, cascade, "ocean h0 texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::field(std::uint32_t cascade,
                                                           std::uint32_t field) const {
    return field_texture_at(fields_, cascade, field, "ocean spectrum field texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::ping(std::uint32_t cascade,
                                                          std::uint32_t field) const {
    return field_texture_at(ping_, cascade, field, "ocean FFT ping texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::pong(std::uint32_t cascade,
                                                          std::uint32_t field) const {
    return field_texture_at(pong_, cascade, field, "ocean FFT pong texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::displacement(std::uint32_t cascade) const {
    return texture_at(displacement_, cascade, "ocean displacement texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::normal(std::uint32_t cascade) const {
    return texture_at(normal_, cascade, "ocean normal texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::foam(std::uint32_t cascade) const {
    return texture_at(foam_, cascade, "ocean foam texture");
}

const cubey::render::Texture2D& OceanSurfaceRuntime::fallback_field() const {
    if (!fallback_field_.has_value()) {
        throw std::runtime_error("ocean fallback field texture is not initialized");
    }
    return fallback_field_.value();
}

bool OceanSurfaceRuntime::cascade_allocated(std::uint32_t cascade) const {
    if (cascade >= cubey::render::kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return cascade_allocated_[cascade];
}

std::uint32_t OceanSurfaceRuntime::cascade_resolution(std::uint32_t cascade) const {
    if (cascade >= cubey::render::kOceanCascadeCount) {
        throw std::runtime_error("ocean cascade index out of range");
    }
    return cascade_resolutions_[cascade];
}

const std::vector<cubey::vulkan::GpuPassTiming>& OceanSurfaceRuntime::latest_timings() const {
    static const std::vector<cubey::vulkan::GpuPassTiming> kEmptyTimings;
    if (!profiler_.has_value()) {
        return kEmptyTimings;
    }
    return profiler_->latest_timings();
}

BackdropReflection ocean_surface_reflection(const render::OceanSurfaceConfig& config,
                                            const OceanSurfaceRuntimeFrameInfo& frame,
                                            const render::OceanSurfaceFrame& surface_frame) {
    const math::Vec4 water = render::srgb_to_linear_rgba(
        {config.water_color_r, config.water_color_g, config.water_color_b, 1.0F});
    const math::Vec3 sky = glm::max(render::atmosphere_environment_evaluate_sh(
                                        frame.lighting.diffuse_irradiance_sh, {0.0F, 1.0F, 0.0F}),
                                    math::Vec3{0.0F});
    const float sky_luminance =
        std::clamp((sky.r * 0.2126F) + (sky.g * 0.7152F) + (sky.b * 0.0722F), 0.0F, 4.0F);
    const float primary_up = std::max(frame.lighting.primary_light_direction.y, 0.0F);
    const math::Vec3 primary = glm::max(frame.lighting.primary_light_color, math::Vec3{0.0F}) *
                               std::max(frame.lighting.primary_light_intensity, 0.0F) * primary_up;
    const math::Vec3 water_body = math::Vec3{water} * (0.12F + (0.28F * sky_luminance)) +
                                  (sky * 0.04F) + (math::Vec3{water} * primary * 0.08F);
    const math::Vec3 radiance = water_body + (sky * 0.42F);

    const float radius = surface_frame.local_frame.planet_radius_m;
    const float altitude = surface_frame.horizon.camera_altitude_m;
    const float horizon_distance = surface_frame.horizon.horizon_distance_m;
    const float horizon_sine = -horizon_distance / std::max(radius + altitude, 1.0F);
    return {
        .radiance = glm::max(radiance, math::Vec3{0.0F}),
        .strength = 0.82F,
        .horizon_elevation_sine = std::clamp(horizon_sine, -0.08F, 0.0F),
        .horizon_softness = 0.07F,
    };
}

BackdropReflection OceanSurfaceRuntime::reflection() const {
    return frame_prepared_
               ? ocean_surface_reflection(ocean_config_, frame_, draw_plan_.surface_frame)
               : BackdropReflection{};
}

const cubey::render::Texture2D& OceanSurfaceRuntime::texture_at(const TextureArray& textures,
                                                                std::uint32_t cascade,
                                                                const char* label) const {
    if (cascade >= textures.size() || !textures[cascade].has_value()) {
        throw std::runtime_error(label == nullptr ? "ocean texture is not initialized" : label);
    }
    return textures[cascade].value();
}

const cubey::render::Texture2D&
OceanSurfaceRuntime::field_texture_at(const FieldTextureArray& textures, std::uint32_t cascade,
                                      std::uint32_t field, const char* label) const {
    if (cascade >= cubey::render::kOceanCascadeCount ||
        field >= cubey::render::kOceanSpectrumFieldCount) {
        throw std::runtime_error(label == nullptr ? "ocean field index out of range" : label);
    }
    const std::uint32_t index = field_texture_index(cascade, field);
    if (!textures[index].has_value()) {
        throw std::runtime_error(label == nullptr ? "ocean field texture is not initialized"
                                                  : label);
    }
    return textures[index].value();
}

VkDescriptorSet OceanSurfaceRuntime::descriptor_at(std::span<const VkDescriptorSet> sets,
                                                   std::uint32_t index, const char* label) const {
    if (index >= sets.size() || sets[index] == VK_NULL_HANDLE) {
        throw std::runtime_error(label == nullptr ? "ocean descriptor set is not initialized"
                                                  : label);
    }
    return sets[index];
}

} // namespace cubey
