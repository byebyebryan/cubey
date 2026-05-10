#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace cubey {

struct Entity {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(Entity lhs, Entity rhs) = default;
};

class EntityManager {
  public:
    EntityManager();

    [[nodiscard]] Entity reserve();
    [[nodiscard]] Entity create();

    void publish(Entity entity);
    void rollback_reserved(Entity entity);
    void destroy(Entity entity, std::uint64_t retire_epoch = 0);

    [[nodiscard]] bool is_alive(Entity entity) const;
    [[nodiscard]] bool is_reserved(Entity entity) const;
    [[nodiscard]] bool is_current(Entity entity) const;
    [[nodiscard]] std::size_t retire_destroyed_up_to(std::uint64_t epoch);

  private:
    enum class SlotState {
        Free,
        Reserved,
        Alive,
        PendingDestroy,
    };

    struct Slot {
        std::uint32_t generation = 0;
        SlotState state = SlotState::Free;
        std::uint64_t retire_epoch = 0;
    };

    [[nodiscard]] bool matches_locked(Entity entity, SlotState state) const;
    [[nodiscard]] Slot& slot_locked(Entity entity);
    [[nodiscard]] const Slot& slot_locked(Entity entity) const;

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::deque<std::uint32_t> free_indices_;
};

} // namespace cubey
