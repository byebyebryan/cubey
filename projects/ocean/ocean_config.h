#pragma once

#include <cubey/core/run_config.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/render/ocean_surface_config.h>

namespace cubey::projects::ocean {

using namespace cubey::render;
using OceanConfig = cubey::render::OceanSurfaceConfig;

[[nodiscard]] inline OceanConfig ocean_config_from_run_config(const RunConfig& config) {
    OceanConfig ocean;
    if (!config.ocean.sea_state.empty()) {
        apply_ocean_sea_state(ocean, ocean_sea_state_from_name(config.ocean.sea_state));
    }
    if (config.ocean.map_size != 0U) {
        ocean.map_size = config.ocean.map_size;
    }
    if (config.ocean.mesh_cells != 0U) {
        ocean.mesh_cells = config.ocean.mesh_cells;
    }
    if (config.ocean.mesh_lod_levels != 0U) {
        ocean.mesh_lod_levels = config.ocean.mesh_lod_levels;
    }
    if (run_config_float_is_set(config.ocean.horizon_target_near_cell_m)) {
        ocean.horizon_target_near_cell_m = config.ocean.horizon_target_near_cell_m;
    }
    ocean.surface_shading_policy =
        ocean_surface_shading_policy_from_name(config.ocean.surface_shading_policy);
    if (run_config_float_is_set(config.ocean.self_shadow_strength)) {
        ocean.self_shadow_strength = config.ocean.self_shadow_strength;
    }
    if (config.ocean.self_shadow_steps != 0U) {
        ocean.self_shadow_steps = config.ocean.self_shadow_steps;
    }
    if (config.ocean.self_shadow_far_steps != 0U) {
        ocean.self_shadow_far_steps = config.ocean.self_shadow_far_steps;
    }
    if (run_config_float_is_set(config.ocean.shape_anti_repeat_strength)) {
        ocean.shape_anti_repeat_strength = config.ocean.shape_anti_repeat_strength;
    }
    if (run_config_float_is_set(config.ocean.detail_anti_repeat_strength)) {
        ocean.detail_anti_repeat_strength = config.ocean.detail_anti_repeat_strength;
    }
    ocean.detail_filter = ocean_detail_filter_from_name(config.ocean.detail_filter);
    if (config.ocean.spectral_domains >= 0) {
        ocean.spectral_domains_enabled = config.ocean.spectral_domains != 0;
    }
    if (config.ocean.terrain_fields >= 0) {
        ocean.terrain_fields_enabled = config.ocean.terrain_fields != 0;
    }
    ocean.field_precision = ocean_field_precision_from_name(config.ocean.field_precision);
    ocean.surface_mode = ocean_surface_mode_from_name(config.ocean.surface_mode);
    if (run_config_float_is_set(config.ocean.planet_radius_scale)) {
        ocean.planet_radius_scale = config.ocean.planet_radius_scale;
    }
    if (run_config_float_is_set(config.ocean.curvature_start_ratio)) {
        ocean.curvature_start_ratio = config.ocean.curvature_start_ratio;
    }
    if (run_config_float_is_set(config.ocean.curvature_end_ratio)) {
        ocean.curvature_end_ratio = config.ocean.curvature_end_ratio;
    }
    if (run_config_float_is_set(config.ocean.curvature_strength)) {
        ocean.curvature_strength = config.ocean.curvature_strength;
    }
    if (run_config_float_is_set(config.ocean.cloud_reflection_strength)) {
        ocean.cloud_reflection_strength = config.ocean.cloud_reflection_strength;
    }
    ocean.cloud_reflection_source =
        ocean_cloud_reflection_source_from_name(config.ocean.cloud_reflection_source);
    if (config.ocean.cloud_environment_extent != 0U) {
        ocean.cloud_environment_extent = config.ocean.cloud_environment_extent;
    }
    if (run_config_float_is_set(config.ocean.cloud_environment_update_hz)) {
        ocean.cloud_environment_update_hz = config.ocean.cloud_environment_update_hz;
    }
    if (run_config_float_is_set(config.ocean.cloud_planar_resolution_scale)) {
        ocean.cloud_planar_resolution_scale = config.ocean.cloud_planar_resolution_scale;
    }
    if (config.ocean.cloud_planar_view_steps != 0U) {
        ocean.cloud_planar_view_steps = config.ocean.cloud_planar_view_steps;
    }
    if (run_config_float_is_set(config.ocean.cloud_planar_guard_band)) {
        ocean.cloud_planar_guard_band = config.ocean.cloud_planar_guard_band;
    }
    if (run_config_float_is_set(config.ocean.cloud_shadow_strength)) {
        ocean.cloud_shadow_strength = config.ocean.cloud_shadow_strength;
    }
    ocean.render_view = ocean_render_view_from_name(config.debug_view);
    ocean.exposure = config.pbr.exposure;
    validate_ocean_config(ocean);
    return ocean;
}

[[nodiscard]] inline cubey::CloudEnvironmentConfig
ocean_cloud_config_from_run_config(const RunConfig& config) {
    cubey::CloudEnvironmentConfig clouds{};
    clouds.enabled = true;
    cubey::apply_cloud_environment_run_config(clouds, config.clouds);
    clouds.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    clouds.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    clouds.layer.orbit_representation = cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
    return clouds;
}

} // namespace cubey::projects::ocean
