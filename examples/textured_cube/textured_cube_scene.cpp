#include "textured_cube_app_internal.h"

#include "../common/cube_scene.h"

#include <stdexcept>

namespace cubey::examples::textured_cube {

void TexturedCubeApp::create_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    const cubey::examples::common::CubeScene3D cube_scene =
        cubey::examples::common::create_cube_scene_3d(
            setup, {
                       .mesh = cube_mesh_handle_,
                       .material = cube_material_handle_,
                       .camera_distance = 4.2F,
                       .directional_light = cubey::directional_light_3d(
                           {0.35F, -0.55F, 0.76F}, {0.72F, 0.72F, 0.72F}, 1.0F),
                   });
    cube_entity_ = cube_scene.cube;
    camera_entity_ = cube_scene.camera;
    light_entity_ = cube_scene.light;
    setup.commit();
}

void TexturedCubeApp::update_scene_transform(const FrameTiming& timing) {
    const float seconds = static_cast<float>(timing.elapsed_seconds);
    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(
        cube_entity_, cubey::examples::common::cube_spin_transform(seconds));
    edits.transforms3d().set_local_transform(
        camera_entity_, cubey::orbit_camera_transform(cubey::OrbitCameraState{
                            .distance = 4.2F,
                            .yaw = orbit_controller_.yaw(),
                            .pitch = orbit_controller_.pitch(),
                        }));
    scene().commit(edits);
}

cubey::Scene& TexturedCubeApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("textured_cube scene is not initialized");
    }
    return *scene_;
}

void TexturedCubeApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    cube_entity_ = {};
    camera_entity_ = {};
    light_entity_ = {};
}

} // namespace cubey::examples::textured_cube
