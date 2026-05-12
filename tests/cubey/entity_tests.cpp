#include <cubey/scene/entity.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_entity_handles_track_null_reserved_and_alive_states() {
    const cubey::Entity null_entity{};
    require(!null_entity, "Default entity should be null");

    cubey::EntityManager entities;
    const cubey::Entity reserved = entities.reserve();
    require(static_cast<bool>(reserved), "Reserved entity should be non-null");
    require(entities.is_reserved(reserved), "Reserved entity should report reserved");
    require(!entities.is_alive(reserved), "Reserved entity should not report alive");

    entities.publish(reserved);
    require(!entities.is_reserved(reserved), "Published entity should no longer report reserved");
    require(entities.is_alive(reserved), "Published entity should report alive");

    entities.destroy(reserved, 7);
    require(!entities.is_alive(reserved), "Destroyed entity should stop reporting alive");
}

void test_entity_manager_invalidates_generations_and_defers_reuse() {
    cubey::EntityManager entities;
    const cubey::Entity first = entities.create();
    entities.destroy(first, 3);

    const cubey::Entity second = entities.create();
    require(second.index != first.index,
            "Destroyed entity slot should not be reused before retirement");

    require(entities.retire_destroyed_up_to(2) == 0,
            "Destroyed slot should not retire before its retire epoch");
    require(entities.retire_destroyed_up_to(3) == 1,
            "Destroyed slot should retire at its retire epoch");

    const cubey::Entity reused = entities.create();
    require(reused.index == first.index, "Retired entity slot should be reusable");
    require(reused.generation != first.generation, "Reused entity should advance generation");
    require(!entities.is_alive(first), "Old generation should stay invalid after slot reuse");
    require(entities.is_alive(reused), "Reused generation should report alive");
}

void test_entity_manager_rolls_back_reserved_entities() {
    cubey::EntityManager entities;
    const cubey::Entity reserved = entities.reserve();
    entities.rollback_reserved(reserved);
    require(!entities.is_reserved(reserved), "Rolled back entity should stop reporting reserved");
    require(!entities.is_alive(reserved), "Rolled back entity should not report alive");

    const cubey::Entity reused = entities.reserve();
    require(reused.index == reserved.index, "Rolled back reserved slot should be reusable");
    require(reused.generation != reserved.generation,
            "Rolled back reserved slot should advance generation");
}

void test_entity_manager_concurrent_reservations_are_unique() {
    cubey::EntityManager entities;
    constexpr std::size_t kThreadCount = 4;
    constexpr std::size_t kEntitiesPerThread = 64;
    std::vector<cubey::Entity> reserved(kThreadCount * kEntitiesPerThread);
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (std::size_t thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        threads.emplace_back([&entities, &reserved, thread_index] {
            const std::size_t begin = thread_index * kEntitiesPerThread;
            for (std::size_t offset = 0; offset < kEntitiesPerThread; ++offset) {
                reserved[begin + offset] = entities.reserve();
            }
        });
    }

    for (std::thread& thread : threads) {
        thread.join();
    }

    std::sort(reserved.begin(), reserved.end(),
              [](cubey::Entity lhs, cubey::Entity rhs) { return lhs.index < rhs.index; });
    for (std::size_t index = 1; index < reserved.size(); ++index) {
        require(reserved[index - 1].index != reserved[index].index,
                "Concurrent reservations should produce unique entity slots");
    }
}
