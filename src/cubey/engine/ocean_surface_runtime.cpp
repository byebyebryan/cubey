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

[[nodiscard]] std::uint32_t ocean_enabled_cascade_mask(const render::OceanSurfaceConfig& config) {
    std::uint32_t mask = 0U;
    for (std::uint32_t cascade = 0U; cascade < render::kOceanCascadeCount; ++cascade) {
        if (render::ocean_cascade_enabled(config, cascade)) {
            mask |= 1U << cascade;
        }
    }
    return mask;
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


} // namespace cubey
