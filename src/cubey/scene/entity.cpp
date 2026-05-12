#include <cubey/scene/entity.h>

#include <limits>
#include <stdexcept>

namespace cubey {

EntityManager::EntityManager() : slots_(1) {}

Entity EntityManager::reserve() {
    std::lock_guard const lock(mutex_);

    std::uint32_t index = 0;
    if (!free_indices_.empty()) {
        index = free_indices_.front();
        free_indices_.pop_front();
    } else {
        if (slots_.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::runtime_error("entity manager exhausted entity indices");
        }
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(Slot{});
    }

    Slot& slot = slots_[index];
    if (slot.generation == 0) {
        slot.generation = 1;
    }
    slot.state = SlotState::Reserved;
    slot.retire_epoch = 0;
    return Entity{.index = index, .generation = slot.generation};
}

Entity EntityManager::create() {
    Entity entity = reserve();
    publish(entity);
    return entity;
}

void EntityManager::publish(Entity entity) {
    std::lock_guard const lock(mutex_);
    Slot& slot = slot_locked(entity);
    if (slot.state != SlotState::Reserved || slot.generation != entity.generation) {
        throw std::runtime_error("entity publish requires a current reserved entity");
    }
    slot.state = SlotState::Alive;
}

void EntityManager::rollback_reserved(Entity entity) {
    std::lock_guard const lock(mutex_);
    Slot& slot = slot_locked(entity);
    if (slot.state != SlotState::Reserved || slot.generation != entity.generation) {
        throw std::runtime_error("entity rollback requires a current reserved entity");
    }
    ++slot.generation;
    slot.state = SlotState::Free;
    slot.retire_epoch = 0;
    free_indices_.push_back(entity.index);
}

void EntityManager::destroy(Entity entity, std::uint64_t retire_epoch) {
    if (!entity) {
        return;
    }

    std::lock_guard const lock(mutex_);
    Slot& slot = slot_locked(entity);
    if (slot.state != SlotState::Alive || slot.generation != entity.generation) {
        throw std::runtime_error("entity destroy requires a current alive entity");
    }
    ++slot.generation;
    slot.state = SlotState::PendingDestroy;
    slot.retire_epoch = retire_epoch;
}

bool EntityManager::is_alive(Entity entity) const {
    std::lock_guard const lock(mutex_);
    return matches_locked(entity, SlotState::Alive);
}

bool EntityManager::is_reserved(Entity entity) const {
    std::lock_guard const lock(mutex_);
    return matches_locked(entity, SlotState::Reserved);
}

bool EntityManager::is_current(Entity entity) const {
    std::lock_guard const lock(mutex_);
    if (!entity || entity.index >= slots_.size()) {
        return false;
    }
    const Slot& slot = slots_[entity.index];
    return slot.generation == entity.generation &&
           (slot.state == SlotState::Alive || slot.state == SlotState::Reserved);
}

std::size_t EntityManager::retire_destroyed_up_to(std::uint64_t epoch) {
    std::lock_guard const lock(mutex_);
    std::size_t retired = 0;
    for (std::uint32_t index = 1; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (slot.state == SlotState::PendingDestroy && slot.retire_epoch <= epoch) {
            slot.state = SlotState::Free;
            slot.retire_epoch = 0;
            free_indices_.push_back(index);
            ++retired;
        }
    }
    return retired;
}

bool EntityManager::matches_locked(Entity entity, SlotState state) const {
    if (!entity || entity.index >= slots_.size()) {
        return false;
    }
    const Slot& slot = slots_[entity.index];
    return slot.state == state && slot.generation == entity.generation;
}

EntityManager::Slot& EntityManager::slot_locked(Entity entity) {
    if (!entity || entity.index >= slots_.size()) {
        throw std::runtime_error("entity handle is invalid");
    }
    return slots_[entity.index];
}

const EntityManager::Slot& EntityManager::slot_locked(Entity entity) const {
    if (!entity || entity.index >= slots_.size()) {
        throw std::runtime_error("entity handle is invalid");
    }
    return slots_[entity.index];
}

} // namespace cubey
