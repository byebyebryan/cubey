#include "shadow_cube_app_internal.h"
#include "shadow_cube_render.h"

#include "../common/cube_scene.h"

#include <cubey/input/orbit_controller.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace cubey::examples::shadow_cube::detail {
namespace {

cubey::Transform3D look_at_transform(cubey::math::Vec3 eye, cubey::math::Vec3 target) {
    const cubey::math::Vec3 forward = glm::normalize(target - eye);
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    if (std::abs(glm::dot(forward, up)) > 0.95F) {
        up = {0.0F, 0.0F, 1.0F};
    }
    return {
        .translation = eye,
        .rotation = glm::quatLookAtRH(forward, up),
    };
}

} // namespace

void ShadowCubeApp::create_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    cube_entity_ = cubey::scene::create_renderable_entity_3d(
        setup, cubey::scene::RenderableEntity3DConfig{
                   .transform =
                       cubey::Transform3D{
                           .scale = {0.82F, 0.82F, 0.82F},
                       },
                   .mesh = cube_mesh_handle_,
                   .material = material_handle_,
                   .local_bounds =
                       cubey::Bounds3D{
                           .center = {0.0F, 0.0F, 0.0F},
                           .half_extent = {1.0F, 1.0F, 1.0F},
                   },
               });
    floor_entity_ = cubey::scene::create_renderable_entity_3d(
        setup, cubey::scene::RenderableEntity3DConfig{
                   .mesh = floor_mesh_handle_,
                   .material = material_handle_,
                   .local_bounds =
                       cubey::Bounds3D{
                           .center = {0.0F, kShadowCubeGroundPlaneY, 0.0F},
                           .half_extent = {4.0F, 0.01F, 4.0F},
                       },
               });
    camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup, cubey::orbit_camera_transform(cubey::OrbitCameraState{.distance = 5.2F}));

    const cubey::math::Vec3 light_eye = light_direction() * 6.0F;
    light_camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup, look_at_transform(light_eye, {0.0F, 0.0F, 0.0F}),
        cubey::Camera3D({
            .projection = cubey::Camera3DProjection::Orthographic,
            .orthographic_height = 7.0F,
            .near_z = 0.1F,
            .far_z = 14.0F,
        }));

    cubey::Light3D sunlight =
        cubey::directional_light_3d(light_direction(), {1.0F, 0.94F, 0.82F}, 1.0F);
    sunlight.casts_shadows = true;
    light_entity_ = cubey::scene::create_directional_light_entity_3d(setup, sunlight);
    setup.commit();
}

void ShadowCubeApp::update_scene_transform(const cubey::FrameTiming& timing) {
    const float seconds = static_cast<float>(timing.elapsed_seconds);
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(
        cube_entity_, cubey::examples::common::cube_spin_transform(
                          seconds, {0.0F, 0.0F, 0.0F}, {0.82F, 0.82F, 0.82F}));
    edits.transforms3d().set_local_transform(
        camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                            .distance = 5.2F,
                            .yaw = orbit_controller_.yaw(),
                            .pitch = orbit_controller_.pitch(),
                        }));
    scene().commit(edits);
}

cubey::scene::FrameRenderPlan3D
ShadowCubeApp::current_frame_plan(const cubey::SceneReadView& view,
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
                .ambient_color = {0.045F, 0.045F, 0.045F},
                .ambient_intensity = 1.0F,
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

} // namespace cubey::examples::shadow_cube::detail
