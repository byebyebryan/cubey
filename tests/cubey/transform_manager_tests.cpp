#include <cubey/math.h>
#include <cubey/scene.h>
#include <cubey/transform_2d.h>
#include <cubey/transform_3d.h>

#include <cmath>
#include <functional>
#include <stdexcept>

namespace {

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
        throw std::runtime_error(message);
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

void test_transform_manager_2d_publishes_parented_world_matrices() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity root = setup.entities().create();
    const cubey::Entity child = setup.entities().create();
    setup.transforms2d().create(root, cubey::Transform2D{.translation = {2.0F, 0.0F}});
    setup.transforms2d().create(child, cubey::Transform2D{.translation = {3.0F, 0.0F}}, root);
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const cubey::TransformInstance2D child_transform = view.transforms2d().instance(child);
    const cubey::math::Vec3 world = view.transforms2d().world_affine_matrix(child_transform) *
                                    cubey::math::Vec3{0.0F, 0.0F, 1.0F};
    require_close(world.x, 5.0F, "2D transform manager should compose parent translation");
    require_close(world.y, 0.0F, "2D transform manager should preserve local y");
}

void test_transform_manager_3d_publishes_parented_world_matrices() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity root = setup.entities().create();
    const cubey::Entity child = setup.entities().create();
    const cubey::Entity grandchild = setup.entities().create();
    setup.transforms3d().create(root, cubey::Transform3D{.translation = {1.0F, 2.0F, 3.0F}});
    setup.transforms3d().create(child, cubey::Transform3D{.translation = {0.0F, 1.0F, 0.0F}}, root);
    setup.transforms3d().create(grandchild, cubey::Transform3D{.translation = {0.0F, 0.0F, 2.0F}},
                                child);
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const cubey::math::Vec4 world =
        view.transforms3d().world_affine_matrix(view.transforms3d().instance(grandchild)) *
        cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(world.x, 1.0F, "3D transform manager should inherit root x");
    require_close(world.y, 3.0F, "3D transform manager should inherit child y");
    require_close(world.z, 5.0F, "3D transform manager should inherit local z");
}

void test_transform_manager_reparenting_preserves_local_transform() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity root_a = setup.entities().create();
    const cubey::Entity root_b = setup.entities().create();
    const cubey::Entity child = setup.entities().create();
    setup.transforms3d().create(root_a, cubey::Transform3D{.translation = {10.0F, 0.0F, 0.0F}});
    setup.transforms3d().create(root_b, cubey::Transform3D{.translation = {20.0F, 0.0F, 0.0F}});
    setup.transforms3d().create(child, cubey::Transform3D{.translation = {1.0F, 0.0F, 0.0F}},
                                root_a);
    setup.commit();

    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.transforms3d().set_parent(child, root_b);
    scene.commit(edits);

    cubey::SceneReadView view = scene.read();
    const cubey::TransformInstance3D child_transform = view.transforms3d().instance(child);
    require_close(view.transforms3d().local_transform(child_transform).translation.x, 1.0F,
                  "Reparenting should preserve local transform");
    const cubey::math::Vec4 world = view.transforms3d().world_affine_matrix(child_transform) *
                                    cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(world.x, 21.0F, "Reparented child should inherit new parent");
}

void test_transform_manager_read_views_keep_epoch_local_snapshots() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.transforms3d().create(entity, cubey::Transform3D{.translation = {1.0F, 0.0F, 0.0F}});
    setup.commit();

    cubey::SceneReadView before = scene.read();
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.transforms3d().set_local_transform(entity,
                                             cubey::Transform3D{.translation = {4.0F, 0.0F, 0.0F}});
    scene.commit(edits);
    cubey::SceneReadView after = scene.read();

    const cubey::math::Vec4 before_world =
        before.transforms3d().world_affine_matrix(before.transforms3d().instance(entity)) *
        cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    const cubey::math::Vec4 after_world =
        after.transforms3d().world_affine_matrix(after.transforms3d().instance(entity)) *
        cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(before_world.x, 1.0F, "Older read view should keep old transform snapshot");
    require_close(after_world.x, 4.0F, "Newer read view should observe committed transform");
}

void test_transform_manager_rejects_invalid_parenting_and_child_destroy() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity root = setup.entities().create();
    const cubey::Entity child = setup.entities().create();
    setup.transforms2d().create(root, cubey::Transform2D{});
    setup.transforms2d().create(child, cubey::Transform2D{}, root);
    setup.commit();

    cubey::SceneEditQueue self_parent = scene.create_edit_queue();
    self_parent.transforms2d().set_parent(root, root);
    require_throws([&scene, &self_parent] { scene.commit(self_parent); },
                   "Transform manager should reject self-parenting");

    cubey::SceneEditQueue cycle = scene.create_edit_queue();
    cycle.transforms2d().set_parent(root, child);
    require_throws([&scene, &cycle] { scene.commit(cycle); },
                   "Transform manager should reject cycles");

    cubey::SceneEditQueue destroy_parent = scene.create_edit_queue();
    destroy_parent.transforms2d().destroy(root);
    require_throws([&scene, &destroy_parent] { scene.commit(destroy_parent); },
                   "Transform manager should reject destroying transforms with children");
}
