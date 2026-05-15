#include "spinning_cube_app_internal.h"

#include "../common/cube_scene.h"

#include <stdexcept>

namespace cubey::examples::spinning_cube {

void SpinningCubeApp::create_scene() {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    const cubey::examples::common::CubeScene3D cube_scene =
        cubey::examples::common::create_cube_scene_3d(setup,
                                                      {
                                                          .mesh = cube_mesh_handle_,
                                                          .material = cube_material_handle_,
                                                          .camera_distance = 4.2F,
                                                      });
    cube_entity_ = cube_scene.cube;
    camera_entity_ = cube_scene.camera;
    setup.commit();
}

void SpinningCubeApp::update_scene_transform() {
    const auto now = std::chrono::steady_clock::now();
    const float seconds =
        static_cast<float>(std::chrono::duration<double>(now - start_time_).count());

    cubey::SceneEditQueue edits = scene().create_edit_queue();
    edits.transforms3d().set_local_transform(
        cube_entity_, cubey::examples::common::cube_spin_transform(seconds));
    scene().commit(edits);
}

cubey::Scene& SpinningCubeApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("spinning_cube scene is not initialized");
    }
    return *scene_;
}

void SpinningCubeApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    cube_entity_ = {};
    camera_entity_ = {};
}

} // namespace cubey::examples::spinning_cube
