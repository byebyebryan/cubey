#include "gltf_viewer_app_internal.h"

#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/scene_builder.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::projects::gltf_viewer {

void GltfViewerApp::create_fallback_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    cubey::Entity cube = setup.entities().create();
    setup.transforms3d().create(cube, {});
    setup.renderables3d().create(
        cube, cubey::Renderable3D{
                  .primitives =
                      {
                          cubey::RenderablePrimitive3D{
                              .mesh = import_resources_.mesh_primitives.front().front().mesh,
                              .material = import_result_.first_material_handle,
                          },
                      },
                  .local_bounds = import_resources_.mesh_primitives.front().front().local_bounds,
              });
    import_result_.root_entities.push_back(cube);
    create_camera_and_light(setup);
    setup.commit();
}

void GltfViewerApp::create_camera_and_light(cubey::SceneTransaction& setup) {
    const float radius = std::max(glm::length(scene_bounds_.half_extent), 1.0F);
    const float camera_distance = std::max(radius * 2.8F, 4.2F);
    orbit_controller_.set_distance_limits(std::max(radius * 0.05F, 0.05F),
                                          std::max(radius * 10.0F, camera_distance * 2.0F));
    orbit_controller_.set_home_distance(camera_distance);
    camera_entity_ =
        cubey::scene::create_camera_entity_3d(setup,
                                              cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                                  .target = scene_bounds_.center,
                                                  .distance = orbit_controller_.distance(),
                                              }),
                                              cubey::Camera3D({
                                                  .near_z = std::max(radius * 0.001F, 0.01F),
                                                  .far_z = std::max(radius * 12.0F, 100.0F),
                                              }));

    const cubey::math::Vec3 light_direction =
        glm::normalize(atmosphere_lighting_.primary_light_direction);
    const cubey::math::Vec3 light_eye =
        scene_bounds_.center + (light_direction * std::max(radius * 4.0F, 6.0F));
    light_camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup, look_at_transform(light_eye, scene_bounds_.center),
        cubey::Camera3D({
            .projection = cubey::Camera3DProjection::Orthographic,
            .orthographic_height = std::max(radius * 3.0F, 4.0F),
            .near_z = 0.1F,
            .far_z = std::max(radius * 10.0F, 16.0F),
        }));

    cubey::Light3D sunlight = cubey::directional_light_3d(
        light_direction, atmosphere_lighting_.primary_light_color,
        atmosphere_lighting_.primary_light_intensity);
    sunlight.casts_shadows = true;
    light_entity_ = cubey::scene::create_directional_light_entity_3d(setup, sunlight);
}

void GltfViewerApp::update_animation(float delta_seconds) {
    if (!asset_.has_value() || asset_->animations.empty()) {
        return;
    }
    if (animation_playback_.animation_index >= asset_->animations.size()) {
        throw std::runtime_error("requested glTF animation index is out of range");
    }
    if (config_.gltf.animation_paused) {
        return;
    }

    const cubey::asset::GltfAnimation& animation =
        asset_->animations[animation_playback_.animation_index];
    cubey::animation::advance_gltf_animation_playback(animation_playback_, delta_seconds,
                                                      animation.duration_seconds);
    const cubey::animation::GltfAnimationSample sample = cubey::animation::sample_gltf_animation(
        asset_.value(), animation, animation_playback_.time_seconds);
    animation_sample_ = sample;

    cubey::SceneEditQueue edits = scene().create_edit_queue();
    cubey::apply_gltf_rigid_animation_sample(edits, asset_.value(), import_result_,
                                             animation_sample_.value());
    scene().commit(edits);
}

void GltfViewerApp::update_camera_transform() {
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(camera_entity_,
                                             cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                                 .target = scene_bounds_.center,
                                                 .distance = orbit_controller_.distance(),
                                                 .yaw = orbit_controller_.yaw(),
                                                 .pitch = orbit_controller_.pitch(),
                                             }));
    scene().commit(edits);
}

cubey::scene::FrameRenderPlan3D GltfViewerApp::current_frame_plan(const cubey::SceneReadView& view,
                                                                  VkExtent2D color_extent) const {
    const cubey::scene::View3D shadow_view{
        .camera_entity = light_camera_entity_,
        .width = kShadowMapSize,
        .height = kShadowMapSize,
        .culling_enabled = false,
    };
    const cubey::scene::View3D scene_view{
        .camera_entity = camera_entity_,
        .width = color_extent.width,
        .height = color_extent.height,
        .environment =
            cubey::scene::Environment3D{
                .ambient_color = {0.0F, 0.0F, 0.0F},
                .ambient_intensity = 0.0F,
                .diffuse_irradiance_sh = atmosphere_lighting_.diffuse_irradiance_sh,
                .diffuse_irradiance_sh_enabled = true,
            },
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

cubey::render::AtmosphereEnvironmentFrameUniforms GltfViewerApp::atmosphere_background_uniforms(
    const cubey::SceneReadView& view, VkExtent2D color_extent) const {
    if (color_extent.width == 0 || color_extent.height == 0) {
        throw std::runtime_error("glTF viewer atmosphere background requires a nonzero extent");
    }

    const float aspect = static_cast<float>(color_extent.width) /
                         static_cast<float>(color_extent.height);
    const cubey::CameraInstance3D camera_instance = view.cameras3d().instance(camera_entity_);
    const cubey::Camera3D& camera = view.cameras3d().camera(camera_instance);
    const cubey::math::Mat4& world =
        view.transforms3d().world_affine_matrix(view.transforms3d().instance(camera_entity_));
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
    return cubey::render::atmosphere_environment_frame_uniforms(
        atmosphere_environment_, {
                                     .view_rays = view_rays,
                                     .render_view =
                                         cubey::render::AtmosphereEnvironmentRenderView::Final,
                                 });
}

cubey::LightPacket3D GltfViewerApp::fallback_light_packet() const {
    return cubey::LightPacket3D{
        .entity = light_entity_,
        .kind = cubey::LightKind3D::Directional,
        .color = atmosphere_lighting_.primary_light_color,
        .intensity = atmosphere_lighting_.primary_light_intensity,
        .direction = glm::normalize(atmosphere_lighting_.primary_light_direction),
    };
}

cubey::Scene& GltfViewerApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("gltf_viewer scene is not initialized");
    }
    return *scene_;
}

const cubey::Scene& GltfViewerApp::scene() const {
    if (scene_ == nullptr) {
        throw std::runtime_error("gltf_viewer scene is not initialized");
    }
    return *scene_;
}

void GltfViewerApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    camera_entity_ = {};
    light_camera_entity_ = {};
    light_entity_ = {};
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
