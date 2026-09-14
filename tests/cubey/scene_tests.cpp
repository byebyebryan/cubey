#include "source_file_test_helpers.h"

#include <cubey/render/resource_registry.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/stable_slot_store.h>
#include <cubey/scene/transform_3d.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_transform_state(const cubey::TransformReadView3D& view,
                             cubey::TransformInstance3D instance,
                             const cubey::Transform3D& expected, const char* message) {
    const cubey::Transform3D& actual = view.local_transform(instance);
    require_close(actual.translation.x, expected.translation.x, message);
    require_close(actual.scale.x, expected.scale.x, message);
}

void require_camera_state(const cubey::CameraReadView3D& view, cubey::CameraInstance3D instance,
                          const cubey::Camera3D& expected, const char* message) {
    const cubey::Camera3D& actual = view.camera(instance);
    require_close(actual.fovy_radians(), expected.fovy_radians(), message);
    require_close(actual.far_z(), expected.far_z(), message);
}

void require_renderable_state(const cubey::RenderableReadView3D& view,
                              cubey::RenderableInstance3D instance,
                              const cubey::Renderable3D& expected, const char* message) {
    const cubey::Renderable3D& actual = view.renderable(instance);
    require(actual.primitives.size() == expected.primitives.size(), message);
    require(!actual.primitives.empty(), message);
    require(actual.primitives[0].mesh == expected.primitives[0].mesh, message);
    require(actual.primitives[0].material == expected.primitives[0].material, message);
    require(actual.primitives[0].instance_count == expected.primitives[0].instance_count, message);
    require_close(actual.local_bounds.center.x, expected.local_bounds.center.x, message);
    require(actual.visible == expected.visible, message);
}

void require_light_state(const cubey::LightReadView3D& view, cubey::LightInstance3D instance,
                         const cubey::Light3D& expected, const char* message) {
    const cubey::Light3D& actual = view.light(instance);
    require(actual.kind == expected.kind, message);
    require_close(actual.color.x, expected.color.x, message);
    require_close(actual.intensity, expected.intensity, message);
    require_close(actual.direction.x, expected.direction.x, message);
}

cubey::Renderable3D make_renderable(cubey::render::MeshHandle mesh,
                                    cubey::render::MaterialHandle material,
                                    std::uint32_t instance_count, cubey::math::Vec3 center,
                                    bool visible) {
    return cubey::Renderable3D{
        .primitives =
            {
                cubey::RenderablePrimitive3D{
                    .mesh = mesh,
                    .material = material,
                    .instance_count = instance_count,
                },
            },
        .local_bounds = cubey::Bounds3D{.center = center},
        .visible = visible,
    };
}

void require_throws(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

using cubey::tests::read_source_file;
using cubey::tests::require_contains;

} // namespace

void test_stable_slot_store_rejects_stale_handles_without_moving_other_slots() {
    cubey::StableSlotStore<int, 2> store;
    const cubey::StableSlotId first = store.create(10);
    const cubey::StableSlotId second = store.create(20);
    const int* second_address = &store.get(second);

    store.destroy(first, 3);
    require_throws([&store, first] { (void)store.get(first); },
                   "Destroyed stable slot handle should be stale immediately");
    require(&store.get(second) == second_address,
            "Destroying one stable slot should not move another slot");

    const cubey::StableSlotId third = store.create(30);
    require(third.index != first.index,
            "Destroyed stable slot should not be reused before retirement");
    require(store.retire_destroyed_up_to(2) == 0,
            "Stable slot should not retire before its retire epoch");
    require(store.retire_destroyed_up_to(3) == 1, "Stable slot should retire at its retire epoch");

    const cubey::StableSlotId reused = store.create(40);
    require(reused.index == first.index, "Retired stable slot should be reusable");
    require(reused.generation != first.generation, "Reused stable slot should advance generation");
}

void test_scene_edit_queue_publishes_reserved_entities_on_commit() {
    cubey::Scene scene;
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    const cubey::Entity entity = edits.create_entity();
    require(scene.entities().is_reserved(entity),
            "Edit-created entity should reserve a handle before commit");
    require(!scene.entities().is_alive(entity),
            "Edit-created entity should not be alive before commit");

    scene.commit(edits);
    require(scene.entities().is_alive(entity), "Committed edit-created entity should be alive");
    require(scene.epoch() == 1, "Scene commit should publish a new epoch");
}

void test_scene_concurrent_edit_queues_publish_through_serialized_commits() {
    constexpr std::size_t kWorkerCount = 4;
    struct WorkerEdits {
        cubey::SceneEditQueue edits;
        cubey::Entity entity;
        cubey::Transform3D transform;
    };

    cubey::Scene scene;
    std::array<std::optional<WorkerEdits>, kWorkerCount> built_edits;
    std::array<std::thread, kWorkerCount> workers;

    for (std::size_t worker_index = 0; worker_index < kWorkerCount; ++worker_index) {
        workers[worker_index] = std::thread([&scene, &built_edits, worker_index] {
            cubey::SceneEditQueue edits = scene.create_edit_queue();
            const cubey::Entity entity = edits.create_entity();
            const float marker = static_cast<float>(worker_index + 1U);
            const cubey::Transform3D transform{
                .translation = {marker, marker + 10.0F, marker + 20.0F},
                .scale = {marker + 1.0F, marker + 2.0F, marker + 3.0F},
            };
            edits.transforms3d().create(entity, transform);
            built_edits[worker_index].emplace(
                WorkerEdits{.edits = std::move(edits), .entity = entity, .transform = transform});
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    for (std::size_t worker_index = 0; worker_index < kWorkerCount; ++worker_index) {
        require(built_edits[worker_index].has_value(),
                "Each worker should publish exactly one completed edit queue slot");
        const cubey::Entity entity = built_edits[worker_index]->entity;
        require(scene.entities().is_reserved(entity),
                "Worker-built edit entities should remain reserved before serialized commit");
        for (std::size_t previous = 0; previous < worker_index; ++previous) {
            require(entity != built_edits[previous]->entity,
                    "Concurrent edit queues should reserve disjoint entity handles");
        }
    }

    const std::uint64_t initial_epoch = scene.epoch();
    for (std::optional<WorkerEdits>& built : built_edits) {
        const std::uint64_t epoch_before_commit = scene.epoch();
        scene.commit(built->edits);
        require(scene.epoch() == epoch_before_commit + 1U,
                "Each serialized worker edit queue commit should publish exactly one epoch");
    }
    require(scene.epoch() == initial_epoch + kWorkerCount,
            "All serialized worker edit queue commits should advance the scene epoch once each");

    cubey::SceneReadView view = scene.read();
    require(view.epoch() == scene.epoch(),
            "Final scene read view should observe the fully published worker edit epoch");
    for (const std::optional<WorkerEdits>& built : built_edits) {
        require(scene.entities().is_alive(built->entity),
                "Serialized worker edit commit should publish every reserved entity");
        const cubey::Transform3D& actual =
            view.transforms3d().local_transform(view.transforms3d().instance(built->entity));
        require_close(actual.translation.x, built->transform.translation.x,
                      "Final read view should retain each worker transform x marker");
        require_close(actual.translation.y, built->transform.translation.y,
                      "Final read view should retain each worker transform y marker");
        require_close(actual.translation.z, built->transform.translation.z,
                      "Final read view should retain each worker transform z marker");
        require_close(actual.scale.x, built->transform.scale.x,
                      "Final read view should retain each worker transform scale marker");
        require_close(actual.scale.y, built->transform.scale.y,
                      "Final read view should retain each worker transform scale y marker");
        require_close(actual.scale.z, built->transform.scale.z,
                      "Final read view should retain each worker transform scale z marker");
    }
}

void test_scene_failed_commit_rolls_back_reserved_entities() {
    cubey::Scene scene;
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    const cubey::Entity entity = edits.create_entity();
    edits.destroy(cubey::Entity{.index = 99, .generation = 1});

    require_throws([&scene, &edits] { scene.commit(edits); },
                   "Scene should reject invalid destroy edits");
    require(!scene.entities().is_reserved(entity),
            "Failed commit should roll back reserved entities");
    require(!scene.entities().is_alive(entity),
            "Failed commit should not publish reserved entities");
}

void test_scene_rejected_mixed_commit_preserves_components_and_read_views() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh("original mesh");
    const cubey::render::MeshHandle replacement_mesh = registry.create_mesh("replacement mesh");
    const cubey::render::MaterialHandle material = registry.create_material("original material");
    const cubey::render::MaterialHandle replacement_material =
        registry.create_material("replacement material");
    cubey::Scene scene(&registry);

    const cubey::Transform3D original_transform{
        .translation = {1.0F, 2.0F, 3.0F},
        .scale = {2.0F, 3.0F, 4.0F},
    };
    const cubey::Camera3D original_camera{cubey::Camera3DConfig{
        .fovy_radians = 0.9F,
        .near_z = 0.5F,
        .far_z = 250.0F,
    }};
    const cubey::Renderable3D original_renderable =
        make_renderable(mesh, material, 2, {1.0F, 2.0F, 3.0F}, true);
    cubey::Light3D original_light =
        cubey::directional_light_3d({0.0F, -1.0F, 0.0F}, {0.2F, 0.3F, 0.4F}, 2.0F);
    original_light.casts_shadows = true;

    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity existing = setup.entities().create();
    setup.transforms3d().create(existing, original_transform);
    setup.cameras3d().create(existing, original_camera);
    setup.renderables3d().create(existing, original_renderable);
    setup.lights3d().create(existing, original_light);
    setup.commit();

    const std::uint64_t initial_epoch = scene.epoch();
    cubey::SceneReadView before = scene.read();
    const cubey::TransformInstance3D before_transform = before.transforms3d().instance(existing);
    const cubey::CameraInstance3D before_camera = before.cameras3d().instance(existing);
    const cubey::RenderableInstance3D before_renderable = before.renderables3d().instance(existing);
    const cubey::LightInstance3D before_light = before.lights3d().instance(existing);

    const cubey::Transform3D replacement_transform{
        .translation = {11.0F, 12.0F, 13.0F},
        .scale = {5.0F, 6.0F, 7.0F},
    };
    const cubey::Camera3D replacement_camera{cubey::Camera3DConfig{
        .fovy_radians = 1.1F,
        .near_z = 0.25F,
        .far_z = 500.0F,
    }};
    const cubey::Renderable3D replacement_renderable =
        make_renderable(replacement_mesh, replacement_material, 7, {7.0F, 8.0F, 9.0F}, false);
    const cubey::Light3D replacement_light =
        cubey::directional_light_3d({1.0F, 0.0F, 0.0F}, {0.6F, 0.7F, 0.8F}, 4.0F);

    cubey::SceneEditQueue rejected = scene.create_edit_queue();
    const cubey::Entity reserved = rejected.create_entity();
    rejected.transforms3d().create(reserved,
                                   cubey::Transform3D{.translation = {-1.0F, -2.0F, -3.0F}});
    rejected.transforms3d().set_local_transform(existing, replacement_transform);
    rejected.cameras3d().create(reserved, cubey::Camera3D{});
    rejected.cameras3d().set_camera(existing, replacement_camera);
    rejected.renderables3d().create(
        reserved, make_renderable(replacement_mesh, replacement_material, 1, {}, true));
    rejected.renderables3d().set_renderable(existing, replacement_renderable);
    rejected.lights3d().create(reserved, replacement_light);
    rejected.lights3d().set_light(existing, replacement_light);
    cubey::Light3D invalid_light = cubey::directional_light_3d({0.0F, 0.0F, 0.0F});
    rejected.lights3d().set_light(existing, invalid_light);

    require_throws([&scene, &rejected] { scene.commit(rejected); },
                   "late invalid light edit should reject the whole mixed scene commit");

    require(scene.epoch() == initial_epoch,
            "rejected mixed scene commit should leave the scene epoch unchanged");
    require(
        !scene.entities().is_current(reserved),
        "rejected mixed scene commit should invalidate and leave unpublished the reserved entity");
    require(scene.entities().is_current(existing),
            "rejected mixed scene commit should preserve the existing entity generation");

    cubey::SceneReadView after = scene.read();

    const cubey::TransformInstance3D after_transform = after.transforms3d().instance(existing);
    require(after_transform == before_transform,
            "rejected mixed scene commit should preserve the transform instance handle");
    require_transform_state(after.transforms3d(), after_transform, original_transform,
                            "fresh read view should preserve transform state");

    const cubey::CameraInstance3D after_camera = after.cameras3d().instance(existing);
    require(after_camera == before_camera,
            "rejected mixed scene commit should preserve the camera instance handle");
    require_camera_state(after.cameras3d(), after_camera, original_camera,
                         "fresh read view should preserve camera state");

    const cubey::RenderableInstance3D after_renderable = after.renderables3d().instance(existing);
    require(after_renderable == before_renderable,
            "rejected mixed scene commit should preserve the renderable instance handle");
    require_renderable_state(after.renderables3d(), after_renderable, original_renderable,
                             "fresh read view should preserve renderable state");

    const cubey::LightInstance3D after_light = after.lights3d().instance(existing);
    require(after_light == before_light,
            "rejected mixed scene commit should preserve the light instance handle");
    require_light_state(after.lights3d(), after_light, original_light,
                        "fresh read view should preserve light state");

    require_transform_state(before.transforms3d(), before_transform, original_transform,
                            "held read view should preserve transform state after rejection");
    require_camera_state(before.cameras3d(), before_camera, original_camera,
                         "held read view should preserve camera state after rejection");
    require_renderable_state(before.renderables3d(), before_renderable, original_renderable,
                             "held read view should preserve renderable state after rejection");
    require_light_state(before.lights3d(), before_light, original_light,
                        "held read view should preserve light state after rejection");

    require_throws([&after, reserved] { (void)after.transforms3d().instance(reserved); },
                   "rejected reserved entity should not gain a transform component");
    require_throws([&after, reserved] { (void)after.cameras3d().instance(reserved); },
                   "rejected reserved entity should not gain a camera component");
    require_throws([&after, reserved] { (void)after.renderables3d().instance(reserved); },
                   "rejected reserved entity should not gain a renderable component");
    require_throws([&after, reserved] { (void)after.lights3d().instance(reserved); },
                   "rejected reserved entity should not gain a light component");
}

void test_scene_read_views_defer_destroyed_entity_reuse_until_release() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity first = setup.entities().create();
    setup.commit();

    {
        cubey::SceneReadView view = scene.read();

        cubey::SceneEditQueue destroy = scene.create_edit_queue();
        destroy.destroy(first);
        scene.commit(destroy);

        cubey::SceneTransaction create_while_view_is_alive = scene.begin_transaction();
        const cubey::Entity second = create_while_view_is_alive.entities().create();
        create_while_view_is_alive.commit();
        require(second.index != first.index,
                "Destroyed entity slot should not be reused while an older read view is active");
    }

    cubey::SceneTransaction create_after_view_release = scene.begin_transaction();
    const cubey::Entity reused = create_after_view_release.entities().create();
    create_after_view_release.commit();
    require(reused.index == first.index,
            "Destroyed entity slot should be reusable after older read views release");
}

void test_scene_read_view_release_serializes_retirement_with_commits() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string source = read_source_file(root / "src/cubey/scene/scene.cpp");
    const std::string expected = "void Scene::release_read_view(std::uint64_t epoch) noexcept {\n"
                                 "    std::lock_guard const edit_lock(edit_mutex_);";

    require_contains(source, expected,
                     "scene read-view release should hold edit_mutex_ before retiring stores");
}
