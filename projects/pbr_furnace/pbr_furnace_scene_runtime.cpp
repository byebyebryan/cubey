#include "pbr_furnace_app_internal.h"

#include <cubey/scene/scene_builder.h>

#include <stdexcept>

namespace cubey::projects::pbr_furnace {

void PbrFurnaceApp::create_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    const auto materials = pbr_furnace_material_grid();
    for (std::size_t index = 0; index < materials.size(); ++index) {
        const PbrFurnaceMaterial& material = materials[index];
        static_cast<void>(cubey::scene::create_renderable_entity_3d(
            setup, {
                       .transform =
                           cubey::Transform3D{
                               .translation = material.position,
                           },
                       .mesh = sphere_mesh_handle_,
                       .material = material_handles_.at(index),
                       .local_bounds =
                           cubey::Bounds3D{
                               .center = {0.0F, 0.0F, 0.0F},
                               .half_extent = {kSphereRadius, kSphereRadius, kSphereRadius},
                           },
                       .cast_shadows = false,
                       .receive_shadows = false,
                   }));
    }
    camera_entity_ = cubey::scene::create_camera_entity_3d(
        setup, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                   .target = {0.0F, 0.0F, 0.0F},
                   .distance = 9.0F,
               }));
    setup.commit();
}

void PbrFurnaceApp::update_camera_transform() {
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(camera_entity_,
                                             cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                                 .target = {0.0F, 0.0F, 0.0F},
                                                 .distance = 9.0F,
                                                 .yaw = orbit_controller_.yaw(),
                                                 .pitch = orbit_controller_.pitch(),
                                             }));
    scene().commit(edits);
}

cubey::scene::RenderFramePlan3D PbrFurnaceApp::current_frame_plan(const cubey::SceneReadView& view,
                                                                  VkExtent2D extent) const {
    const cubey::scene::View3D render_view{
        .camera_entity = camera_entity_,
        .width = extent.width,
        .height = extent.height,
        .environment =
            cubey::scene::Environment3D{
                .ambient_color = {0.0F, 0.0F, 0.0F},
                .ambient_intensity = 0.0F,
            },
        .culling_enabled = false,
    };
    cubey::scene::RenderFramePlan3D plan =
        cubey::scene::build_render_frame_plan_3d(render_view, view, engine_.render_resources());
    if (plan.draw_packets.size() != kPbrFurnaceMaterialCount) {
        throw std::runtime_error("pbr_furnace scene should produce one packet per material");
    }
    return plan;
}

cubey::render::PbrSceneUniforms
PbrFurnaceApp::scene_uniforms(const cubey::SceneReadView& scene_view,
                              const cubey::scene::RenderFramePlan3D& plan,
                              VkFormat color_format) const {
    const cubey::math::Vec3 camera_position = camera_world_position(scene_view);
    return {
        .view_projection = plan.view_projection_matrix,
        .light_view_projection = cubey::math::Mat4{1.0F},
        .camera_position = {camera_position, 1.0F},
        .light_direction = {0.0F, -1.0F, 0.0F, 0.0F},
        .light_color_intensity = {1.0F, 1.0F, 1.0F, 0.0F},
        .ambient_color_intensity = {0.0F, 0.0F, 0.0F, 0.0F},
        .environment_intensity_mip_count =
            {
                white_environment().intensity,
                static_cast<float>(white_environment().prefiltered_mip_levels),
                0.0F,
                0.0F,
            },
        .display_transform = cubey::render::pbr_display_transform_uniform(
            cubey::render::pbr_display_transform_for_target(color_format, 0.0F,
                                                            cubey::render::PbrTonemap::Linear)),
    };
}

cubey::math::Vec3 PbrFurnaceApp::camera_world_position(const cubey::SceneReadView& view) const {
    const cubey::TransformInstance3D instance = view.transforms3d().instance(camera_entity_);
    const cubey::math::Mat4& world = view.transforms3d().world_affine_matrix(instance);
    return {world[3].x, world[3].y, world[3].z};
}

cubey::Scene& PbrFurnaceApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("pbr_furnace scene is not initialized");
    }
    return *scene_;
}

void PbrFurnaceApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    camera_entity_ = {};
}

} // namespace cubey::projects::pbr_furnace
