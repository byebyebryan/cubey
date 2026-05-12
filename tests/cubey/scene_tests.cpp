#include <cubey/detail/stable_slot_store.h>
#include <cubey/scene/scene.h>

#include <functional>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
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

void test_stable_slot_store_rejects_stale_handles_without_moving_other_slots() {
    cubey::detail::StableSlotStore<int, 2> store;
    const cubey::detail::StableSlotId first = store.create(10);
    const cubey::detail::StableSlotId second = store.create(20);
    const int* second_address = &store.get(second);

    store.destroy(first, 3);
    require_throws([&store, first] { (void)store.get(first); },
                   "Destroyed stable slot handle should be stale immediately");
    require(&store.get(second) == second_address,
            "Destroying one stable slot should not move another slot");

    const cubey::detail::StableSlotId third = store.create(30);
    require(third.index != first.index,
            "Destroyed stable slot should not be reused before retirement");
    require(store.retire_destroyed_up_to(2) == 0,
            "Stable slot should not retire before its retire epoch");
    require(store.retire_destroyed_up_to(3) == 1, "Stable slot should retire at its retire epoch");

    const cubey::detail::StableSlotId reused = store.create(40);
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
