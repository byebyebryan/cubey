#include "instanced_cubes_app_internal.h"

#include "../common/cube_scene.h"

#include <stdexcept>

namespace cubey::examples::instanced_cubes {

void InstancedCubesApp::create_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    const cubey::examples::common::CubeScene3D cube_scene =
        cubey::examples::common::create_cube_scene_3d(
            setup, {
                       .mesh = cube_mesh_handle_,
                       .material = cube_material_handle_,
                       .cube_bounds =
                           cubey::Bounds3D{
                               .center = {0.0F, 0.0F, 0.0F},
                               .half_extent = {5.6F, 3.0F, 0.6F},
                           },
                       .camera_distance = kCameraDistance,
                       .instance_count = instance_buffer().count(),
                   });
    cube_entity_ = cube_scene.cube;
    camera_entity_ = cube_scene.camera;
    setup.commit();
}

void InstancedCubesApp::update_camera_transform() {
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(camera_entity_,
                                             cubey::orbit_camera_transform(cubey::OrbitCameraState{
                                                 .distance = orbit_controller_.distance(),
                                                 .yaw = orbit_controller_.yaw(),
                                                 .pitch = orbit_controller_.pitch(),
                                             }));
    scene().commit(edits);
}

cubey::Scene& InstancedCubesApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("instanced_cubes scene is not initialized");
    }
    return *scene_;
}

void InstancedCubesApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    cube_entity_ = {};
    camera_entity_ = {};
}

} // namespace cubey::examples::instanced_cubes
