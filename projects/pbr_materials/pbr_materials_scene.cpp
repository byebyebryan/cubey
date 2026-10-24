#include "pbr_materials_app_internal.h"

#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <stdexcept>

namespace cubey::projects::pbr_materials::detail {
namespace {

const cubey::math::Vec3 kLightDirection = glm::normalize(cubey::math::Vec3{-.35F, .42F, .84F});

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

void PbrMaterialsApp::create_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    for (std::uint32_t index = 0; index < material_handles_.size(); ++index) {
        const cubey::Entity cube = cubey::scene::create_renderable_entity_3d(
            setup, cubey::scene::RenderableEntity3DConfig{
                       .transform =
                           cubey::Transform3D{
                               .translation = math::Vec3{0},
                               .scale = kMaterialSpecimenScale,
                           },
                       .mesh = sphere_mesh_handle_,
                       .material = material_handles_.at(index),
                       .local_bounds =
                           cubey::Bounds3D{
                               .center = {0.0F, 0.0F, 0.0F},
                               .half_extent = {0.25F, 0.25F, 0.25F},
                           },
                       .cast_shadows = false,
                       .receive_shadows = false,
                   });
        specimens_.push_back({
            .entity = cube,
            .material = material_handles_.at(index),
        });
    }

    camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup,
        cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .distance = kCameraDistance,
        }),
        cubey::Camera3D({.fovy_radians = glm::radians(45.0F), .near_z = .01F, .far_z = 100}));
    const cubey::math::Vec3 light_eye = kLightDirection * 9.0F;
    light_camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup, look_at_transform(light_eye, {0.0F, 0.0F, 0.0F}),
        cubey::Camera3D({
            .projection = cubey::Camera3DProjection::Orthographic,
            .orthographic_height = 8.5F,
            .near_z = 0.1F,
            .far_z = 32.0F,
        }));
    const auto& sky = atmosphere_.lighting();
    cubey::Light3D light =
        config_.environment == "studio"
            ? cubey::directional_light_3d(kLightDirection, {1.8F, 1.6F, 1.3F}, 1)
            : cubey::directional_light_3d(sky.primary_light_direction, sky.primary_light_color,
                                          sky.primary_light_intensity);
    light.casts_shadows = false;
    light_entity_ = cubey::scene::create_directional_light_entity_3d(setup, light);
    setup.commit();
    refresh_specimens();
}

void PbrMaterialsApp::update_scene_transform(const FrameTiming& timing) {
    (void)timing;
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(
        camera_entity_,
        cubey::orbit_camera_transform(cubey::OrbitCameraState{
            .distance = orbit_controller_.distance(),
            .yaw = config_.yaw + orbit_controller_.yaw() +
                   (config_.presentation.empty() ? 0 : presentation_yaw(presentation_seconds_)),
            .pitch = orbit_controller_.pitch(),
        }));
    scene().commit(edits);
}

cubey::scene::FrameRenderPlan3D
PbrMaterialsApp::current_frame_plan(const cubey::SceneReadView& view,
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
            config_.environment == "studio"
                ? cubey::scene::Environment3D{.ambient_color = {0, 0, 0}, .ambient_intensity = 0}
                : atmosphere_.scene_environment(),
        .culling_enabled = false,
    };
    cubey::scene::FrameRenderPlan3D frame_plan({
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
    return frame_plan;
}

cubey::LightPacket3D PbrMaterialsApp::fallback_light_packet() const {
    return cubey::LightPacket3D{
        .entity = light_entity_,
        .kind = cubey::LightKind3D::Directional,
        .color = {1.8F, 1.6F, 1.3F},
        .intensity = 1.0F,
        .direction = kLightDirection,
    };
}

cubey::Scene& PbrMaterialsApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("pbr_materials scene is not initialized");
    }
    return *scene_;
}

void PbrMaterialsApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    specimens_.clear();
    camera_entity_ = {};
    light_camera_entity_ = {};
    light_entity_ = {};
}

} // namespace cubey::projects::pbr_materials::detail
