#include "gltf_viewer_app_internal.h"

#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/scene_builder.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace cubey::projects::gltf_viewer {
namespace {

constexpr float kCameraBaseYaw = cubey::render::kAtmosphereEnvironmentSunriseViewYawRadians;
constexpr float kCameraBasePitch = cubey::render::kAtmosphereEnvironmentSunriseViewPitchRadians;

[[nodiscard]] float capture_camera_angle_radians(const std::optional<float>& override_degrees,
                                                 float default_radians) {
    if (!override_degrees.has_value()) {
        return default_radians;
    }
    return *override_degrees * (std::numbers::pi_v<float> / 180.0F);
}

} // namespace

void GltfViewerApp::create_fallback_scene(GltfViewerSceneGeneration& generation) {
    generation.scene = &engine_.create_scene();
    cubey::SceneTransaction setup = generation.scene->begin_transaction();
    cubey::Entity cube = setup.entities().create();
    setup.transforms3d().create(cube, {});
    setup.renderables3d().create(
        cube,
        cubey::Renderable3D{
            .primitives =
                {
                    cubey::RenderablePrimitive3D{
                        .mesh = generation.import_resources.mesh_primitives.front().front().mesh,
                        .material = generation.import_result.first_material_handle,
                    },
                },
            .local_bounds =
                generation.import_resources.mesh_primitives.front().front().local_bounds,
        });
    generation.import_result.root_entities.push_back(cube);
    create_camera_and_light(generation, setup);
    setup.commit();
}

void GltfViewerApp::create_camera_and_light(GltfViewerSceneGeneration& generation,
                                            cubey::SceneTransaction& setup) {
    const float radius = std::max(glm::length(generation.bounds.half_extent), 1.0F);
    if (ocean_backdrop_enabled() && !ocean_foreground_height_explicit_) {
        ocean_foreground_height_m_ = std::max(20.0F, radius * 2.0F);
    }
    if (ocean_backdrop_enabled()) {
        const cubey::render::BackdropSurfacePlacement placement =
            cubey::render::resolve_backdrop_surface_placement({
                .surface =
                    {
                        .maximum_local_height_m =
                            cubey::render::ocean_surface_placement_crest_allowance_m(ocean_config_),
                    },
                .foreground =
                    {
                        .anchor_world_height_m = generation.bounds.center.y,
                        .minimum_local_height_m = -generation.bounds.half_extent.y,
                    },
                .requested_foreground_height_m = ocean_foreground_height_m_,
                .minimum_clearance_m = 0.1F,
            });
        ocean_minimum_foreground_height_m_ = placement.required_foreground_height_m;
        ocean_foreground_height_m_ = placement.effective_foreground_height_m;
    }
    const float camera_distance =
        std::max(radius * 2.8F, 4.2F) * config_.capture.camera_distance_scale.value_or(1.0F);
    orbit_controller_.set_distance_limits(std::max(radius * 0.05F, 0.05F),
                                          std::max(radius * 10.0F, camera_distance * 2.0F));
    orbit_controller_.set_home_distance(camera_distance);
    generation.camera_entity = cubey::scene::create_camera_entity_3d(
        setup,
        cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = generation.bounds.center,
            .distance = orbit_controller_.distance(),
            .yaw =
                capture_camera_angle_radians(config_.capture.camera_yaw_degrees, kCameraBaseYaw) +
                orbit_controller_.yaw(),
            .pitch = capture_camera_angle_radians(config_.capture.camera_pitch_degrees,
                                                  kCameraBasePitch) +
                     orbit_controller_.pitch(),
        }),
        cubey::Camera3D({
            .near_z = terrain_backdrop_enabled() || ocean_backdrop_enabled()
                          ? 0.1F
                          : std::max(radius * 0.001F, 0.01F),
            .far_z = terrain_backdrop_enabled() || ocean_backdrop_enabled()
                         ? std::max(radius * 12.0F, 16'384.0F * 5.0F)
                         : std::max(radius * 12.0F, 100.0F),
        }));

    const cubey::render::AtmosphereEnvironmentLighting& lighting = atmosphere_runtime_.lighting();
    const cubey::math::Vec3 light_direction = glm::normalize(lighting.primary_light_direction);
    const cubey::math::Vec3 light_eye =
        generation.bounds.center + (light_direction * std::max(radius * 4.0F, 6.0F));
    generation.light_camera_entity = cubey::scene::create_camera_entity_3d(
        setup, look_at_transform(light_eye, generation.bounds.center),
        cubey::Camera3D({
            .projection = cubey::Camera3DProjection::Orthographic,
            .orthographic_height = std::max(radius * 3.0F, 4.0F),
            .near_z = 0.1F,
            .far_z = std::max(radius * 10.0F, 16.0F),
        }));

    cubey::Light3D sunlight = cubey::directional_light_3d(
        light_direction, lighting.primary_light_color, lighting.primary_light_intensity);
    sunlight.casts_shadows = true;
    generation.light_entity = cubey::scene::create_directional_light_entity_3d(setup, sunlight);
}

void GltfViewerApp::update_animation(float delta_seconds) {
    GltfViewerSceneGeneration& generation = active_generation();
    if (!generation.asset.has_value() || generation.asset->animations.empty()) {
        return;
    }
    if (generation.animation_playback.animation_index >= generation.asset->animations.size()) {
        throw std::runtime_error("requested glTF animation index is out of range");
    }
    if (config_.gltf.animation_paused) {
        return;
    }

    const cubey::asset::GltfAnimation& animation =
        generation.asset->animations[generation.animation_playback.animation_index];
    cubey::animation::advance_gltf_animation_playback(generation.animation_playback, delta_seconds,
                                                      animation.duration_seconds);
    const cubey::animation::GltfAnimationSample sample = cubey::animation::sample_gltf_animation(
        generation.asset.value(), animation, generation.animation_playback.time_seconds);
    generation.animation_sample = sample;

    cubey::SceneEditQueue edits = scene().create_edit_queue();
    cubey::apply_gltf_rigid_animation_sample(edits, generation.asset.value(),
                                             generation.import_result,
                                             generation.animation_sample.value());
    scene().commit(edits);
}

void GltfViewerApp::refresh_atmosphere_lighting_scene() {
    if (!active_generation_ || !active_generation().light_entity ||
        !active_generation().light_camera_entity) {
        return;
    }

    GltfViewerSceneGeneration& generation = active_generation();
    const float radius = std::max(glm::length(generation.bounds.half_extent), 1.0F);
    const cubey::render::AtmosphereEnvironmentLighting& lighting = atmosphere_runtime_.lighting();
    const cubey::math::Vec3 light_direction = glm::normalize(lighting.primary_light_direction);
    const cubey::math::Vec3 light_eye =
        generation.bounds.center + (light_direction * std::max(radius * 4.0F, 6.0F));

    cubey::Light3D sunlight = cubey::directional_light_3d(
        light_direction, lighting.primary_light_color, lighting.primary_light_intensity);
    sunlight.casts_shadows = true;

    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.lights3d().set_light(generation.light_entity, sunlight);
    edits.transforms3d().set_local_transform(
        generation.light_camera_entity, look_at_transform(light_eye, generation.bounds.center));
    scene().commit(edits);
}

void GltfViewerApp::update_camera_transform() {
    GltfViewerSceneGeneration& generation = active_generation();
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(
        generation.camera_entity,
        cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .target = generation.bounds.center,
            .distance = orbit_controller_.distance(),
            .yaw =
                capture_camera_angle_radians(config_.capture.camera_yaw_degrees, kCameraBaseYaw) +
                orbit_controller_.yaw() + capture_orbit_offset_radians_,
            .pitch = capture_camera_angle_radians(config_.capture.camera_pitch_degrees,
                                                  kCameraBasePitch) +
                     orbit_controller_.pitch(),
        }));
    scene().commit(edits);
}

cubey::scene::FrameRenderPlan3D GltfViewerApp::current_frame_plan(const cubey::SceneReadView& view,
                                                                  VkExtent2D color_extent) const {
    const cubey::scene::View3D shadow_view{
        .camera_entity = active_generation().light_camera_entity,
        .width = kShadowMapSize,
        .height = kShadowMapSize,
        .culling_enabled = false,
    };
    const cubey::scene::View3D scene_view{
        .camera_entity = active_generation().camera_entity,
        .width = color_extent.width,
        .height = color_extent.height,
        .environment = atmosphere_runtime_.scene_environment(),
    };
    return cubey::scene::FrameRenderPlan3D({
        cubey::scene::RenderPassPlan3D{
            .label = "shadow",
            .kind = cubey::scene::RenderPassKind3D::DepthOnly,
            .frame_plan = cubey::scene::build_render_frame_plan_3d(shadow_view, view,
                                                                   engine_.render_resources()),
        },
        cubey::scene::RenderPassPlan3D{
            .label = "scene",
            .kind = cubey::scene::RenderPassKind3D::Color,
            .frame_plan = cubey::scene::build_render_frame_plan_3d(scene_view, view,
                                                                   engine_.render_resources()),
        },
    });
}

cubey::render::AtmosphereEnvironmentFrameUniforms
GltfViewerApp::atmosphere_background_uniforms(const cubey::SceneReadView& view,
                                              VkExtent2D color_extent) const {
    if (color_extent.width == 0 || color_extent.height == 0) {
        throw std::runtime_error("glTF viewer atmosphere background requires a nonzero extent");
    }

    const float aspect =
        static_cast<float>(color_extent.width) / static_cast<float>(color_extent.height);
    const GltfViewerSceneGeneration& generation = active_generation();
    const cubey::CameraInstance3D camera_instance =
        view.cameras3d().instance(generation.camera_entity);
    const cubey::Camera3D& camera = view.cameras3d().camera(camera_instance);
    const cubey::math::Mat4& world = view.transforms3d().world_affine_matrix(
        view.transforms3d().instance(generation.camera_entity));
    const cubey::math::Vec3 right = glm::normalize(cubey::math::Vec3{world[0]});
    const cubey::math::Vec3 up = glm::normalize(cubey::math::Vec3{world[1]});
    const cubey::math::Vec3 forward = glm::normalize(-cubey::math::Vec3{world[2]});
    const cubey::render::ViewRayBasis3D view_rays{
        .right_aspect = {right.x, right.y, right.z, aspect},
        .up_tan_half_fovy =
            {
                up.x,
                up.y,
                up.z,
                std::tan(camera.fovy_radians() * 0.5F),
            },
        .forward = {forward.x, forward.y, forward.z, 0.0F},
    };
    if (terrain_backdrop_enabled() || ocean_backdrop_enabled()) {
        const cubey::math::Vec3 camera_position{world[3]};
        const float surface_reference_height =
            terrain_backdrop_enabled() ? generation.bounds.center.y - terrain_foreground_height_m_
                                       : generation.bounds.center.y - ocean_foreground_height_m_;
        return cubey::render::atmosphere_environment_frame_uniforms(
            atmosphere_state_.environment,
            {
                .view_rays = view_rays,
                .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
                .camera_position_km = {0.0F,
                                       atmosphere_state_.environment.bottom_radius_km +
                                           std::max(camera_position.y - surface_reference_height,
                                                    0.0F) *
                                               0.001F,
                                       0.0F},
                .camera_position_km_explicit = true,
            });
    }
    return atmosphere_runtime_
        .frame({
            .view_rays = view_rays,
            .render_view = cubey::render::AtmosphereEnvironmentRenderView::Final,
        })
        .background;
}

cubey::CloudEnvironmentConfig GltfViewerApp::cloud_environment_config() const {
    cubey::CloudEnvironmentConfig cloud = clouds_config_;
    cloud.layer.planet_radius_m = atmosphere_state_.environment.bottom_radius_km * 1000.0F;
    cloud.layer.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
    cloud.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    cloud.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Local;
    return cloud;
}

cubey::CloudEnvironmentRuntimeFrame
GltfViewerApp::cloud_environment_frame(const cubey::SceneReadView& view,
                                       VkExtent2D color_extent) const {
    const GltfViewerSceneGeneration& generation = active_generation();
    const cubey::CameraInstance3D camera_instance =
        view.cameras3d().instance(generation.camera_entity);
    const cubey::Camera3D& camera = view.cameras3d().camera(camera_instance);
    const cubey::math::Mat4& world = view.transforms3d().world_affine_matrix(
        view.transforms3d().instance(generation.camera_entity));
    const cubey::math::Vec3 right = glm::normalize(cubey::math::Vec3{world[0]});
    const cubey::math::Vec3 up = glm::normalize(cubey::math::Vec3{world[1]});
    const cubey::math::Vec3 forward = glm::normalize(-cubey::math::Vec3{world[2]});
    return atmosphere_runtime_.clouds().frame(
        cubey::CloudEnvironmentSurfaceViewInfo{
            .camera_position =
                terrain_backdrop_enabled() || ocean_backdrop_enabled()
                    ? cubey::math::Vec3{
                          cubey::math::Vec3{world[3]}.x,
                          std::max(cubey::math::Vec3{world[3]}.y -
                                       (generation.bounds.center.y -
                                        (terrain_backdrop_enabled()
                                             ? terrain_foreground_height_m_
                                             : ocean_foreground_height_m_)),
                                   0.0F),
                          cubey::math::Vec3{world[3]}.z,
                      }
                    : cubey::math::Vec3{
                          0.0F, atmosphere_state_.environment.camera_altitude_km * 1000.0F, 0.0F},
            .camera_right = right,
            .camera_up = up,
            .camera_forward = forward,
            .tan_half_fovy = std::tan(camera.fovy_radians() * 0.5F),
            .target_extent = color_extent,
            .near_plane_m = camera.near_z(),
            .far_plane_m = camera.far_z(),
            .external_background = true,
            .scene_depth_mode = cubey::render::CloudLayerSceneDepthMode::OpaqueForeground,
        },
        atmosphere_runtime_.lighting());
}

cubey::ForwardPbrRenderer3DTerrainBackdrop GltfViewerApp::terrain_backdrop_frame(
    const cubey::SceneReadView& view, const cubey::scene::FrameRenderPlan3D& frame_plan,
    const cubey::render::AtmosphereEnvironmentFrameUniforms& atmosphere) {
    const cubey::ForwardPbrRenderer3DFramePlans plans =
        cubey::forward_pbr_renderer_3d_frame_plans(frame_plan);
    const cubey::math::Mat4& camera_world = view.transforms3d().world_affine_matrix(
        view.transforms3d().instance(active_generation().camera_entity));
    const cubey::render::BackdropSurfacePlacement placement =
        cubey::render::resolve_backdrop_surface_placement({
            .surface = active_generation().terrain_surface.value(),
            .foreground =
                {
                    .anchor_world_height_m = active_generation().bounds.center.y,
                    .minimum_local_height_m = -active_generation().bounds.half_extent.y,
                },
            .requested_foreground_height_m = terrain_foreground_height_m_,
            .minimum_clearance_m = 0.1F,
        });
    return {
        .runtime = &terrain_runtime_,
        .frame =
            {
                .view_projection = plans.scene->view_projection_matrix,
                .camera_position = cubey::math::Vec3{camera_world[3]},
                .world_translation = {active_generation().bounds.center.x,
                                      placement.surface_world_translation_y,
                                      active_generation().bounds.center.z},
                .atmosphere = atmosphere,
                .lighting = atmosphere_runtime_.lighting(),
                .material = terrain_material_,
                .shadows_enabled = terrain_shadows_,
                .reflections_enabled = terrain_reflections_,
            },
    };
}

cubey::ForwardPbrRenderer3DOceanSurface
GltfViewerApp::ocean_surface_frame(const cubey::SceneReadView& view, VkExtent2D color_extent) {
    const GltfViewerSceneGeneration& generation = active_generation();
    const cubey::CameraInstance3D camera_instance =
        view.cameras3d().instance(generation.camera_entity);
    const cubey::Camera3D& camera = view.cameras3d().camera(camera_instance);
    const cubey::render::AtmosphereReflectionProbeSnapshot atmosphere =
        atmosphere_runtime_.reflection_probe().snapshot();
    const cubey::render::CloudEnvironmentProbeSnapshot clouds =
        atmosphere_runtime_.clouds().snapshot();
    const cubey::render::BackdropSurfacePlacement placement =
        cubey::render::resolve_backdrop_surface_placement({
            .surface =
                {
                    .maximum_local_height_m =
                        cubey::render::ocean_surface_placement_crest_allowance_m(ocean_config_),
                },
            .foreground =
                {
                    .anchor_world_height_m = generation.bounds.center.y,
                    .minimum_local_height_m = -generation.bounds.half_extent.y,
                },
            .requested_foreground_height_m = ocean_foreground_height_m_,
            .minimum_clearance_m = 0.1F,
        });
    return {
        .runtime = &ocean_runtime_,
        .frame =
            {
                .viewport_extent = color_extent,
                .vertical_fov_radians = camera.fovy_radians(),
                .planet_radius_m = atmosphere_state_.environment.bottom_radius_km * 1000.0F *
                                   ocean_config_.planet_radius_scale,
                .water_datum_m = placement.surface_world_translation_y,
                .elapsed_seconds = ocean_elapsed_seconds_,
                .delta_seconds = ocean_delta_seconds_,
                .lighting = atmosphere_runtime_.lighting(),
                .atmosphere_environment_blend = atmosphere.blend,
                .cloud_environment_blend = clouds.valid ? clouds.blend : 1.0F,
                .cloud_environment_valid = clouds.valid,
                .debug_view = cubey::render::OceanRenderView::Final,
                .exposure = display_exposure(),
            },
    };
}

cubey::LightPacket3D GltfViewerApp::fallback_light_packet() const {
    const cubey::render::AtmosphereEnvironmentLighting& lighting = atmosphere_runtime_.lighting();
    return cubey::LightPacket3D{
        .entity = active_generation().light_entity,
        .kind = cubey::LightKind3D::Directional,
        .color = lighting.primary_light_color,
        .intensity = lighting.primary_light_intensity,
        .direction = glm::normalize(lighting.primary_light_direction),
    };
}

GltfViewerSceneGeneration& GltfViewerApp::active_generation() {
    if (!active_generation_) {
        throw std::runtime_error("gltf_viewer active generation is not initialized");
    }
    return *active_generation_;
}

const GltfViewerSceneGeneration& GltfViewerApp::active_generation() const {
    if (!active_generation_) {
        throw std::runtime_error("gltf_viewer active generation is not initialized");
    }
    return *active_generation_;
}

cubey::Scene& GltfViewerApp::scene() {
    if (active_generation().scene == nullptr) {
        throw std::runtime_error("gltf_viewer scene is not initialized");
    }
    return *active_generation().scene;
}

const cubey::Scene& GltfViewerApp::scene() const {
    if (active_generation().scene == nullptr) {
        throw std::runtime_error("gltf_viewer scene is not initialized");
    }
    return *active_generation().scene;
}

void GltfViewerApp::destroy_scene_generation(GltfViewerSceneGeneration& generation) {
    if (generation.scene == nullptr) {
        return;
    }
    engine_.destroy_scene(*generation.scene);
    generation.scene = nullptr;
    generation.camera_entity = {};
    generation.light_camera_entity = {};
    generation.light_entity = {};
}

const cubey::render::GeneratedPbrEnvironment& GltfViewerApp::ibl_environment() const {
    if (!ibl_environment_.has_value()) {
        throw std::runtime_error("PBR IBL environment is not initialized");
    }
    return ibl_environment_.value();
}

cubey::ForwardPbrRenderer3D& GltfViewerApp::forward_pbr_renderer() const {
    if (forward_pbr_renderer_ == nullptr) {
        throw std::runtime_error("forward PBR renderer is not initialized");
    }
    return *forward_pbr_renderer_;
}

} // namespace cubey::projects::gltf_viewer
