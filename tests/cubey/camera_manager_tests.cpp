#include <cubey/camera_2d.h>
#include <cubey/camera_3d.h>
#include <cubey/math.h>
#include <cubey/scene.h>
#include <cubey/transform_3d.h>

#include <cmath>
#include <functional>
#include <numbers>
#include <stdexcept>

namespace {

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_matrix_close(const cubey::math::Mat4& actual, const cubey::math::Mat4& expected,
                          const char* message) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            require_close(actual[column][row], expected[column][row], message);
        }
    }
}

void require_throws(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_camera_managers_publish_2d_and_3d_camera_snapshots() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity camera_2d_entity = setup.entities().create();
    const cubey::Entity camera_3d_entity = setup.entities().create();
    setup.cameras2d().create(camera_2d_entity,
                             cubey::Camera2D({.center = {2.0F, 3.0F}, .scale = 0.5F}));
    setup.transforms3d().create(camera_3d_entity,
                                cubey::Transform3D{.translation = {0.0F, 0.0F, 4.2F}});
    setup.cameras3d().create(camera_3d_entity, cubey::Camera3D({
                                                   .fovy_radians = std::numbers::pi_v<float> / 3.0F,
                                                   .near_z = 0.1F,
                                                   .far_z = 100.0F,
                                               }));
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const cubey::CameraInstance2D camera_2d = view.cameras2d().instance(camera_2d_entity);
    const cubey::Camera2DView camera_2d_view = view.cameras2d().view(camera_2d, 640.0F, 320.0F);
    require_close(camera_2d_view.center.x, 2.0F, "2D camera manager should publish center x");
    require_close(camera_2d_view.center.y, 3.0F, "2D camera manager should publish center y");
    require_close(camera_2d_view.scale, 0.5F, "2D camera manager should publish scale");
    require_close(camera_2d_view.aspect, 2.0F, "2D camera manager should compute aspect");

    const cubey::CameraInstance3D camera_3d = view.cameras3d().instance(camera_3d_entity);
    const cubey::math::Mat4 expected_view = cubey::math::translation(0.0F, 0.0F, -4.2F);
    require_matrix_close(view.cameras3d().view_matrix(camera_3d, view.transforms3d()),
                         expected_view,
                         "3D camera manager should read world transform for view matrix");
}

void test_camera_manager_updates_keep_epoch_local_snapshots() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.cameras3d().create(entity, cubey::Camera3D{});
    setup.commit();

    cubey::SceneReadView before = scene.read();
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.cameras3d().set_camera(entity, cubey::Camera3D({
                                             .fovy_radians = std::numbers::pi_v<float> / 4.0F,
                                             .near_z = 0.5F,
                                             .far_z = 250.0F,
                                         }));
    scene.commit(edits);
    cubey::SceneReadView after = scene.read();

    const cubey::CameraInstance3D before_camera = before.cameras3d().instance(entity);
    const cubey::CameraInstance3D after_camera = after.cameras3d().instance(entity);
    require_close(before.cameras3d().camera(before_camera).near_z(), 0.1F,
                  "older read view should keep previous camera state");
    require_close(after.cameras3d().camera(after_camera).near_z(), 0.5F,
                  "new read view should publish updated camera state");
}

void test_camera_manager_entity_destroy_retires_attached_components() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.cameras2d().create(entity, cubey::Camera2D{});
    setup.commit();

    cubey::SceneReadView before = scene.read();
    const cubey::CameraInstance2D old_camera = before.cameras2d().instance(entity);

    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.destroy(entity);
    scene.commit(edits);
    cubey::SceneReadView after = scene.read();

    require_close(before.cameras2d().view(old_camera, 1.0F, 1.0F).scale, 1.0F,
                  "older read view should keep destroyed entity camera snapshot");
    require_throws([&after, entity] { (void)after.cameras2d().instance(entity); },
                   "new read view should not expose camera for destroyed entity");
}

void test_camera_manager_rejects_duplicate_and_stale_entity_edits() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.cameras2d().create(entity, cubey::Camera2D{});
    setup.commit();

    cubey::SceneEditQueue duplicate = scene.create_edit_queue();
    duplicate.cameras2d().create(entity, cubey::Camera2D{});
    require_throws([&scene, &duplicate] { scene.commit(duplicate); },
                   "camera manager should reject duplicate components");

    cubey::SceneEditQueue destroy = scene.create_edit_queue();
    destroy.destroy(entity);
    scene.commit(destroy);

    cubey::SceneEditQueue stale = scene.create_edit_queue();
    stale.cameras2d().create(entity, cubey::Camera2D{});
    require_throws([&scene, &stale] { scene.commit(stale); },
                   "camera manager should reject stale entity handles");
}
