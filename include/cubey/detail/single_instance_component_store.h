#pragma once

#include <cubey/detail/stable_slot_store.h>
#include <cubey/entity.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cubey::detail {

template <typename ComponentT, typename InstanceT, typename SnapshotT>
class SingleInstanceComponentStore {
  public:
    [[nodiscard]] bool has_component(Entity entity) const {
        return entity_to_slot_.contains(entity);
    }

    [[nodiscard]] InstanceT instance(Entity entity) const {
        return InstanceT{.slot = slot_for(entity)};
    }

    [[nodiscard]] std::shared_ptr<const SnapshotT> snapshot() const {
        return snapshot_;
    }

    [[nodiscard]] std::vector<Entity> active_entities() const {
        std::vector<Entity> entities;
        for (const StableSlotId slot_id : slots_.active_instances()) {
            entities.push_back(slots_.get(slot_id).entity);
        }
        return entities;
    }

    void create(Entity entity, const ComponentT& component) {
        const StableSlotId slot_id = slots_.create(component);
        entity_to_slot_[entity] = slot_id;
    }

    [[nodiscard]] ComponentT& component_for(Entity entity) {
        return slots_.get(slot_for(entity));
    }

    void destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch) {
        const auto position = entity_to_slot_.find(entity);
        if (position == entity_to_slot_.end()) {
            return;
        }
        slots_.destroy(position->second, retire_epoch);
        entity_to_slot_.erase(position);
    }

    template <typename MakeSnapshotComponent>
    void publish_snapshot(MakeSnapshotComponent&& make_snapshot_component) {
        auto next_snapshot = std::make_shared<SnapshotT>();
        for (const StableSlotId slot_id : slots_.active_instances()) {
            const ComponentT& component = slots_.get(slot_id);
            const std::size_t index = next_snapshot->components.size();
            next_snapshot->components.push_back(make_snapshot_component(component));
            next_snapshot->active_instances.push_back(InstanceT{.slot = slot_id});
            next_snapshot->entity_to_component[component.entity] = index;
            next_snapshot->slot_to_component[slot_id] = index;
        }
        snapshot_ = std::move(next_snapshot);
    }

    void retire_destroyed_up_to(std::uint64_t epoch) {
        static_cast<void>(slots_.retire_destroyed_up_to(epoch));
    }

  private:
    [[nodiscard]] StableSlotId slot_for(Entity entity) const {
        const auto position = entity_to_slot_.find(entity);
        if (position == entity_to_slot_.end()) {
            throw std::runtime_error("entity does not have a component");
        }
        return position->second;
    }

    StableSlotStore<ComponentT> slots_{};
    std::unordered_map<Entity, StableSlotId, EntityHash> entity_to_slot_{};
    std::shared_ptr<const SnapshotT> snapshot_ = std::make_shared<SnapshotT>();
};

} // namespace cubey::detail
