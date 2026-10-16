#include "ocean_app_internal.h"

namespace cubey::projects::ocean::detail {

void OceanApp::apply_camera_preset(OceanCameraPreset preset) {
    const OceanCameraPresetConfig config = ocean_camera_preset_config(preset);
    camera_preset_ = config.preset;
    camera_base_yaw_ = config.yaw;
    camera_base_pitch_ = config.pitch;
    orbit_controller_.set_home_distance(config.distance);
    orbit_controller_.reset();
}

[[nodiscard]] cubey::Transform3D OceanApp::camera_transform() const {
    const float distance = orbit_controller_.distance();
    const float pitch = ocean_above_surface_orbit_pitch(
        distance, camera_base_pitch_ + orbit_controller_.pitch(),
        kCameraMinimumSurfaceClearanceMeters);
    return cubey::orbit_camera_transform(cubey::OrbitCameraState{
        .target = {0.0F, 0.0F, 0.0F},
        .distance = distance,
        .yaw = camera_base_yaw_ + orbit_controller_.yaw() + capture_orbit_offset_radians_,
        .pitch = pitch,
    });
}

[[nodiscard]] float OceanApp::planet_radius_m() const {
    return ocean_planet_radius_m(atmosphere_state_.environment.bottom_radius_km) *
           ocean_config_.planet_radius_scale;
}

[[nodiscard]] OceanSurfaceFrame OceanApp::ocean_surface_frame() const {
    const cubey::Transform3D transform = camera_transform();
    return ocean_surface_frame_from_camera(ocean_config_, transform.translation,
                                           planet_radius_m());
}

[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
OceanApp::atmosphere_environment_for_surface_frame(const OceanSurfaceFrame& surface_frame) const {
    cubey::render::AtmosphereEnvironmentConfig environment = atmosphere_state_.environment;
    environment.camera_altitude_km =
        surface_frame.horizon.camera_altitude_m / kOceanMetersPerKilometer;
    return environment;
}

[[nodiscard]] cubey::math::Mat4
OceanApp::ocean_view_projection_matrix(VkExtent2D extent, const cubey::Transform3D& transform,
                                       float far_plane_m) const {
    cubey::Camera3D camera = camera_;
    camera.set_projection(camera.fovy_radians(), kCameraNearPlane,
                          std::max(far_plane_m, kCameraFarPlane));
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    return camera.view_projection_matrix(transform, aspect);
}

[[nodiscard]] cubey::math::Mat4
OceanApp::ocean_view_projection_matrix(VkExtent2D extent, const cubey::Transform3D& transform,
                                       const OceanSurfaceFrame& surface_frame) const {
    return ocean_view_projection_matrix(extent, transform,
                                        surface_frame.projection_far_plane_m);
}

[[nodiscard]] OceanMeshDrawPlan OceanApp::ocean_mesh_draw_plan(VkExtent2D extent) const {
    const cubey::Transform3D transform = camera_transform();
    OceanMeshDrawPlan plan{};
    plan.surface_frame = ocean_surface_frame_from_camera(ocean_config_, transform.translation,
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
        const OceanPatchShadingPlan shading_plan = ocean_patch_shading_plan(
            ocean_config_, patch, transform.translation.x, transform.translation.z,
            plan.surface_frame.horizon.camera_altitude_m, camera_.fovy_radians(),
            extent.height);
        plan.shading_plans[index] = shading_plan;
        const cubey::Bounds3D bounds = ocean_mesh_patch_world_bounds(
            ocean_config_, plan.surface_frame, patch, transform.translation);
        const bool visible = cubey::scene::intersects(frustum, bounds);
        plan.visible_patches[index] = visible;
        if (visible) {
            ++plan.stats.submitted_patches;
            const std::uint32_t triangles = ocean_mesh_patch_triangle_count(patch);
            plan.stats.submitted_triangles += triangles;
            if (shading_plan.detail_filter_reduced) {
                ++plan.stats.reduced_filter_patches;
                plan.stats.reduced_filter_triangles += triangles;
            }
            if (shading_plan.self_shadow_reduced) {
                ++plan.stats.reduced_shadow_patches;
                plan.stats.reduced_shadow_triangles += triangles;
            }
        }
    }
    plan.stats.culled_patches = plan.stats.generated_patches - plan.stats.submitted_patches;
    return plan;
}

[[nodiscard]] OceanPushConstants
OceanApp::surface_push_constants(VkExtent2D extent, const OceanSurfaceFrame& surface_frame,
                                 const OceanMeshPatch& patch,
                                 const OceanPatchShadingPlan& shading_plan) const {
    const cubey::Transform3D transform = camera_transform();
    const cubey::math::Mat4 view_projection =
        ocean_view_projection_matrix(extent, transform, surface_frame);
    const float debug_z =
        render_view_ == OceanRenderView::Exposure
            ? display_exposure()
            : static_cast<float>(surface_frame.mesh_config.mesh_lod_levels - 1U);

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
OceanApp::surface_feature_uniforms(const OceanSurfaceFrame& surface_frame,
                                   const cubey::render::CloudLayerShadowProduct* cloud_shadow,
                                   cubey::render::FrameSlot frame_slot) const {
    const bool cloud_shadow_valid = cloud_shadow != nullptr && cloud_shadow->transmittance;
    const cubey::render::CloudEnvironmentProbeSnapshot cloud_environment =
        atmosphere_runtime_.clouds().snapshot();
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
                static_cast<float>(ocean_config_.detail_filter),
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
        .atmosphere_environment_options =
            {
                atmosphere_runtime_.reflection_probe().snapshot().blend,
                0.0F,
                0.0F,
                0.0F,
            },
        .cloud_environment_options =
            {
                static_cast<float>(
                    static_cast<std::uint32_t>(ocean_config_.cloud_reflection_source)),
                cloud_environment.valid ? cloud_environment.blend : 1.0F,
                cloud_environment.valid ? 1.0F : 0.0F,
                4.0F,
            },
        .cloud_planar_right_aspect =
            cloud_planar.valid ? cloud_planar.view_rays.right_aspect : cubey::math::Vec4{},
        .cloud_planar_up_tan_half_fovy =
            cloud_planar.valid ? cloud_planar.view_rays.up_tan_half_fovy : cubey::math::Vec4{},
        .cloud_planar_forward_lod =
            cloud_planar.valid
                ? cubey::math::Vec4{cloud_planar.view_rays.forward.x,
                                    cloud_planar.view_rays.forward.y,
                                    cloud_planar.view_rays.forward.z, cloud_planar.max_lod}
                : cubey::math::Vec4{},
        .cloud_planar_options = {cloud_planar.valid ? 1.0F : 0.0F,
                                 ocean_config_.cloud_planar_guard_band, 0.0F, 0.0F},
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
OceanApp::reference_pillar_push_constants(VkExtent2D extent) const {
    const cubey::Transform3D transform = camera_transform();
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    return {
        .view_projection = camera_.view_projection_matrix(transform, aspect),
        .light_direction = atmosphere_sun_light_uniform(),
    };
}

[[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
OceanApp::atmosphere_background_uniforms(VkExtent2D extent,
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

[[nodiscard]] cubey::CloudEnvironmentSurfaceViewInfo
OceanApp::cloud_surface_view(VkExtent2D extent, const OceanSurfaceFrame& surface_frame) const {
    const cubey::Transform3D transform = camera_transform();
    const float aspect = extent.height == 0U ? 1.0F
                                             : static_cast<float>(extent.width) /
                                                   static_cast<float>(extent.height);
    const cubey::render::ViewRayBasis3D view_rays =
        cubey::render::view_ray_basis_3d(transform.rotation, aspect, camera_.fovy_radians());
    return {
        .camera_position = transform.translation,
        .camera_right = cubey::math::Vec3{view_rays.right_aspect},
        .camera_up = cubey::math::Vec3{view_rays.up_tan_half_fovy},
        .camera_forward = cubey::math::Vec3{view_rays.forward},
        .tan_half_fovy = view_rays.up_tan_half_fovy.w,
        .target_extent = extent,
        .near_plane_m = kCameraNearPlane,
        .far_plane_m = surface_frame.projection_far_plane_m,
        .external_background = true,
        .scene_depth_mode = cubey::render::CloudLayerSceneDepthMode::OpaqueForeground,
    };
}

[[nodiscard]] cubey::CloudEnvironmentRuntimeFrame
OceanApp::current_cloud_frame(VkExtent2D extent, const OceanSurfaceFrame& surface_frame) {
    cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    clouds.set_config(ocean_cloud_config(surface_frame));
    clouds.set_elapsed_seconds(time_seconds_);
    return clouds.frame(cloud_surface_view(extent, surface_frame), atmosphere_lighting_);
}

[[nodiscard]] cubey::render::CloudLayerShadowRequest
OceanApp::cloud_shadow_request(const OceanSurfaceFrame& surface_frame) const {
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
OceanApp::spectrum_push_constants(std::uint32_t cascade_index) const {
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

[[nodiscard]] cubey::math::Vec4 OceanApp::atmosphere_sun_light_uniform() const {
    const cubey::math::Vec3& light = atmosphere_lighting_.sun_direction;
    return {light.x, light.y, light.z, atmosphere_lighting_.sun_intensity};
}

[[nodiscard]] OceanModulatePushConstants
OceanApp::modulate_push_constants(std::uint32_t cascade_index) const {
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

[[nodiscard]] OceanFftPushConstants OceanApp::fft_push_constants(std::uint32_t cascade_index,
                                                                 std::uint32_t stage,
                                                                 bool horizontal,
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
OceanApp::unpack_push_constants(std::uint32_t cascade_index) const {
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
} // namespace cubey::projects::ocean::detail
