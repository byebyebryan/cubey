#include "material_cubes_app_internal.h"

#include "../common/cube_scene.h"

#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <stdexcept>

namespace cubey::examples::material_cubes::detail {
namespace {

const cubey::math::Vec3 kLightDirection =
    glm::normalize(cubey::math::Vec3{0.45F, 0.82F, 0.35F});

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

[[nodiscard]] cubey::math::Vec3 material_cube_translation(std::uint32_t index) {
    const std::uint32_t row = index / kMaterialGridColumns;
    const std::uint32_t column = index % kMaterialGridColumns;
    const float centered_column =
        static_cast<float>(column) - (static_cast<float>(kMaterialGridColumns - 1U) * 0.5F);
    const float centered_row =
        (static_cast<float>(kMaterialGridRows - 1U) * 0.5F) - static_cast<float>(row);
    return {
        centered_column * kMaterialGridSpacingX,
        centered_row * kMaterialGridSpacingY,
        0.0F,
    };
}

} // namespace

void MaterialCubesApp::create_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    for (std::uint32_t index = 0; index < kMaterialCubeCount; ++index) {
        const cubey::Entity cube = cubey::scene::create_renderable_entity_3d(
            setup, cubey::scene::RenderableEntity3DConfig{
                       .transform =
                           cubey::Transform3D{
                               .translation = material_cube_translation(index),
                               .scale = kMaterialCubeScale,
                           },
                       .mesh = cube_mesh_handle_,
                       .material = material_handles_.at(index),
                       .local_bounds =
                           cubey::Bounds3D{
                               .center = {0.0F, 0.0F, 0.0F},
                               .half_extent = {1.0F, 1.0F, 1.0F},
                           },
                       .cast_shadows = false,
                       .receive_shadows = false,
                   });
        cubes_.push_back({
            .entity = cube,
            .material = material_handles_.at(index),
        });
    }

    camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                   .distance = kCameraDistance,
               }));
    const cubey::math::Vec3 light_eye = kLightDirection * 9.0F;
    light_camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup, look_at_transform(light_eye, {0.0F, 0.0F, 0.0F}),
        cubey::Camera3D({
            .projection = cubey::Camera3DProjection::Orthographic,
            .orthographic_height = 8.5F,
            .near_z = 0.1F,
            .far_z = 32.0F,
        }));
    cubey::Light3D light =
        cubey::directional_light_3d(kLightDirection, {1.0F, 0.94F, 0.82F}, 1.2F);
    light.casts_shadows = false;
    light_entity_ = cubey::scene::create_directional_light_entity_3d(setup, light);
    setup.commit();
}

void MaterialCubesApp::update_scene_transform(const FrameTiming& timing) {
    const float seconds = static_cast<float>(timing.elapsed_seconds);
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(
        camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                            .distance = kCameraDistance,
                            .yaw = orbit_controller_.yaw(),
                            .pitch = orbit_controller_.pitch(),
                        }));
    for (std::uint32_t index = 0; index < cubes_.size(); ++index) {
        edits.transforms3d().set_local_transform(
            cubes_.at(index).entity,
            cubey::examples::common::cube_spin_transform(seconds, material_cube_translation(index),
                                                         kMaterialCubeScale));
    }
    scene().commit(edits);
}

cubey::scene::FrameRenderPlan3D
MaterialCubesApp::current_frame_plan(const cubey::SceneReadView& view,
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
                .ambient_color = {0.035F, 0.038F, 0.045F},
                .ambient_intensity = 1.0F,
            },
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
    if (frame_plan.passes()[1].frame_plan.draw_packets.size() != kMaterialCubeCount) {
        throw std::runtime_error("material_cubes scene should produce one packet per cube");
    }
    return frame_plan;
}

cubey::LightPacket3D MaterialCubesApp::fallback_light_packet() const {
    return cubey::LightPacket3D{
        .entity = light_entity_,
        .kind = cubey::LightKind3D::Directional,
        .color = {1.0F, 0.94F, 0.82F},
        .intensity = 1.2F,
        .direction = kLightDirection,
    };
}

cubey::Scene& MaterialCubesApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("material_cubes scene is not initialized");
    }
    return *scene_;
}

void MaterialCubesApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    cubes_.clear();
    camera_entity_ = {};
    light_camera_entity_ = {};
    light_entity_ = {};
}

} // namespace cubey::examples::material_cubes::detail
