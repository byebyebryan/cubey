#include <cubey/scene/transform_manager.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace cubey {

template <typename TransformT>
void BasicTransformEditQueue<TransformT>::create(Entity entity,
                                                 const TransformT& local_transform,
                                                 Entity parent) {
    creates_.push_back(CreateEdit{
        .entity = entity,
        .local_transform = local_transform,
        .parent = parent,
    });
}

template <typename TransformT> void BasicTransformEditQueue<TransformT>::destroy(Entity entity) {
    destroys_.push_back(entity);
}

template <typename TransformT>
void BasicTransformEditQueue<TransformT>::set_local_transform(Entity entity,
                                                              const TransformT& local_transform) {
    local_updates_.push_back(LocalUpdateEdit{
        .entity = entity,
        .local_transform = local_transform,
    });
}

template <typename TransformT>
void BasicTransformEditQueue<TransformT>::set_parent(Entity child, Entity parent) {
    parent_updates_.push_back(ParentUpdateEdit{
        .child = child,
        .parent = parent,
    });
}

template <typename TransformT> void BasicTransformEditQueue<TransformT>::clear_parent(Entity child) {
    set_parent(child, {});
}

template <typename TransformT, typename MatrixT, typename InstanceT>
BasicTransformReadView<TransformT, MatrixT, InstanceT>::BasicTransformReadView()
    : snapshot_(std::make_shared<Snapshot>()) {}

template <typename TransformT, typename MatrixT, typename InstanceT>
BasicTransformReadView<TransformT, MatrixT, InstanceT>::BasicTransformReadView(
    std::shared_ptr<const Snapshot> snapshot)
    : snapshot_(std::move(snapshot)) {
    if (snapshot_ == nullptr) {
        snapshot_ = std::make_shared<Snapshot>();
    }
}

template <typename TransformT, typename MatrixT, typename InstanceT>
InstanceT BasicTransformReadView<TransformT, MatrixT, InstanceT>::instance(Entity entity) const {
    const auto position = snapshot_->entity_to_component.find(entity);
    if (position == snapshot_->entity_to_component.end()) {
        throw std::runtime_error("entity does not have a transform component");
    }
    return snapshot_->active_instances[position->second];
}

template <typename TransformT, typename MatrixT, typename InstanceT>
const TransformT& BasicTransformReadView<TransformT, MatrixT, InstanceT>::local_transform(
    InstanceT instance) const {
    return component(instance).local_transform;
}

template <typename TransformT, typename MatrixT, typename InstanceT>
const MatrixT& BasicTransformReadView<TransformT, MatrixT, InstanceT>::world_affine_matrix(
    InstanceT instance) const {
    return component(instance).world_affine_matrix;
}

template <typename TransformT, typename MatrixT, typename InstanceT>
Entity BasicTransformReadView<TransformT, MatrixT, InstanceT>::parent(InstanceT instance) const {
    return component(instance).parent;
}

template <typename TransformT, typename MatrixT, typename InstanceT>
const std::vector<InstanceT>&
BasicTransformReadView<TransformT, MatrixT, InstanceT>::active_instances() const noexcept {
    return snapshot_->active_instances;
}

template <typename TransformT, typename MatrixT, typename InstanceT>
const typename BasicTransformReadView<TransformT, MatrixT, InstanceT>::Component&
BasicTransformReadView<TransformT, MatrixT, InstanceT>::component(InstanceT instance) const {
    const auto position = snapshot_->slot_to_component.find(instance.slot);
    if (position == snapshot_->slot_to_component.end()) {
        throw std::runtime_error("transform instance is not part of this read view");
    }
    return snapshot_->components[position->second];
}

template <typename TransformT>
bool BasicTransformManager<TransformT>::has_component(Entity entity) const {
    return entity_to_slot_.contains(entity);
}

template <typename TransformT>
typename BasicTransformManager<TransformT>::Instance
BasicTransformManager<TransformT>::instance(Entity entity) const {
    return Instance{.slot = slot_for(entity)};
}

template <typename TransformT>
std::shared_ptr<const typename BasicTransformManager<TransformT>::Snapshot>
BasicTransformManager<TransformT>::snapshot() const {
    return snapshot_;
}

template <typename TransformT>
void BasicTransformManager<TransformT>::validate(
    const BasicTransformEditQueue<TransformT>& edits, const EntityManager& entities) const {
    std::unordered_set<Entity, EntityHash> existing{};
    std::unordered_map<Entity, Entity, EntityHash> parent_of{};
    for (const StableSlotId slot_id : slots_.active_instances()) {
        const Component& component = slots_.get(slot_id);
        existing.insert(component.entity);
        if (component.parent) {
            parent_of[component.entity] = component.parent;
        }
    }

    for (const auto& create : edits.creates_) {
        if (!create.entity || !entities.is_current(create.entity)) {
            throw std::runtime_error("transform create requires a current entity");
        }
        if (existing.contains(create.entity)) {
            throw std::runtime_error("entity already has a transform component");
        }
        if (create.parent && !existing.contains(create.parent)) {
            throw std::runtime_error("transform parent entity has no transform component");
        }
        existing.insert(create.entity);
        if (create.parent) {
            parent_of[create.entity] = create.parent;
        }
    }

    for (const auto& update : edits.local_updates_) {
        if (!existing.contains(update.entity)) {
            throw std::runtime_error("transform local update requires an existing component");
        }
    }

    for (const auto& update : edits.parent_updates_) {
        if (!existing.contains(update.child)) {
            throw std::runtime_error("transform reparent requires an existing child");
        }
        if (update.parent && update.parent == update.child) {
            throw std::runtime_error("transform cannot be parented to itself");
        }
        if (update.parent && !existing.contains(update.parent)) {
            throw std::runtime_error("transform parent entity has no transform component");
        }
        if (update.parent) {
            parent_of[update.child] = update.parent;
        } else {
            parent_of.erase(update.child);
        }
    }

    for (const Entity entity : edits.destroys_) {
        if (!existing.contains(entity)) {
            throw std::runtime_error("transform destroy requires an existing component");
        }
        if (has_child_in(entity, parent_of)) {
            throw std::runtime_error("transform with children cannot be destroyed directly");
        }
        existing.erase(entity);
        parent_of.erase(entity);
    }

    reject_cycles(parent_of);
}

template <typename TransformT>
void BasicTransformManager<TransformT>::validate_destroy_entity(Entity entity) const {
    const auto position = entity_to_slot_.find(entity);
    if (position == entity_to_slot_.end()) {
        return;
    }
    const Component& component = slots_.get(position->second);
    if (!component.children.empty()) {
        throw std::runtime_error("entity transform has children and cannot be destroyed");
    }
}

template <typename TransformT>
void BasicTransformManager<TransformT>::apply(const BasicTransformEditQueue<TransformT>& edits,
                                              std::uint64_t retire_epoch) {
    for (const auto& create : edits.creates_) {
        Component component{
            .entity = create.entity,
            .local_transform = create.local_transform,
            .world_affine_matrix = Matrix{1.0F},
            .parent = create.parent,
        };
        const StableSlotId slot_id = slots_.create(component);
        entity_to_slot_[create.entity] = slot_id;
        attach_to_parent(create.entity, create.parent);
    }

    for (const auto& update : edits.local_updates_) {
        Component& component = component_for(update.entity);
        component.local_transform = update.local_transform;
        mark_subtree_dirty(update.entity);
    }

    for (const auto& update : edits.parent_updates_) {
        Component& component = component_for(update.child);
        detach_from_parent(update.child, component.parent);
        component.parent = update.parent;
        attach_to_parent(update.child, update.parent);
        mark_subtree_dirty(update.child);
    }

    for (const Entity entity : edits.destroys_) {
        destroy_entity_if_exists(entity, retire_epoch);
    }
}

template <typename TransformT>
void BasicTransformManager<TransformT>::destroy_entity_if_exists(Entity entity,
                                                                std::uint64_t retire_epoch) {
    const auto position = entity_to_slot_.find(entity);
    if (position == entity_to_slot_.end()) {
        return;
    }

    Component& component = slots_.get(position->second);
    if (!component.children.empty()) {
        throw std::runtime_error("entity transform has children and cannot be destroyed");
    }

    detach_from_parent(entity, component.parent);
    slots_.destroy(position->second, retire_epoch);
    entity_to_slot_.erase(position);
}

template <typename TransformT> void BasicTransformManager<TransformT>::update_world_matrices() {
    for (const StableSlotId slot_id : slots_.active_instances()) {
        const Component& component = slots_.get(slot_id);
        if (!component.parent) {
            update_world_matrix(component.entity, Matrix{1.0F});
        }
    }
}

template <typename TransformT> void BasicTransformManager<TransformT>::publish_snapshot() {
    auto next_snapshot = std::make_shared<Snapshot>();
    for (const StableSlotId slot_id : slots_.active_instances()) {
        const Component& component = slots_.get(slot_id);
        const std::size_t index = next_snapshot->components.size();
        next_snapshot->components.push_back(SnapshotComponent{
            .entity = component.entity,
            .local_transform = component.local_transform,
            .world_affine_matrix = component.world_affine_matrix,
            .parent = component.parent,
        });
        next_snapshot->active_instances.push_back(Instance{.slot = slot_id});
        next_snapshot->entity_to_component[component.entity] = index;
        next_snapshot->slot_to_component[slot_id] = index;
    }
    snapshot_ = std::move(next_snapshot);
}

template <typename TransformT>
void BasicTransformManager<TransformT>::retire_destroyed_up_to(std::uint64_t epoch) {
    static_cast<void>(slots_.retire_destroyed_up_to(epoch));
}

template <typename TransformT>
bool BasicTransformManager<TransformT>::has_child_in(
    Entity entity, const std::unordered_map<Entity, Entity, EntityHash>& parent_of) {
    for (const auto& entry : parent_of) {
        if (entry.second == entity) {
            return true;
        }
    }
    return false;
}

template <typename TransformT>
void BasicTransformManager<TransformT>::reject_cycles(
    const std::unordered_map<Entity, Entity, EntityHash>& parent_of) {
    for (const auto& entry : parent_of) {
        std::unordered_set<Entity, EntityHash> visited{};
        Entity current = entry.first;
        while (true) {
            const auto position = parent_of.find(current);
            if (position == parent_of.end() || !position->second) {
                break;
            }
            current = position->second;
            if (!visited.insert(current).second) {
                throw std::runtime_error("transform hierarchy cannot contain cycles");
            }
        }
    }
}

template <typename TransformT>
StableSlotId BasicTransformManager<TransformT>::slot_for(Entity entity) const {
    const auto position = entity_to_slot_.find(entity);
    if (position == entity_to_slot_.end()) {
        throw std::runtime_error("entity does not have a transform component");
    }
    return position->second;
}

template <typename TransformT>
typename BasicTransformManager<TransformT>::Component&
BasicTransformManager<TransformT>::component_for(Entity entity) {
    return slots_.get(slot_for(entity));
}

template <typename TransformT>
const typename BasicTransformManager<TransformT>::Component&
BasicTransformManager<TransformT>::component_for(Entity entity) const {
    return slots_.get(slot_for(entity));
}

template <typename TransformT>
void BasicTransformManager<TransformT>::attach_to_parent(Entity child, Entity parent) {
    if (!parent) {
        return;
    }
    std::vector<Entity>& siblings = component_for(parent).children;
    if (std::find(siblings.begin(), siblings.end(), child) == siblings.end()) {
        siblings.push_back(child);
    }
}

template <typename TransformT>
void BasicTransformManager<TransformT>::detach_from_parent(Entity child, Entity parent) {
    if (!parent) {
        return;
    }
    std::vector<Entity>& siblings = component_for(parent).children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
}

template <typename TransformT>
void BasicTransformManager<TransformT>::mark_subtree_dirty(Entity entity) {
    Component& component = component_for(entity);
    component.dirty = true;
    const std::vector<Entity> children = component.children;
    for (const Entity child : children) {
        mark_subtree_dirty(child);
    }
}

template <typename TransformT>
void BasicTransformManager<TransformT>::update_world_matrix(Entity entity,
                                                            const Matrix& parent_world) {
    Component& component = component_for(entity);
    if (component.dirty) {
        component.world_affine_matrix = parent_world * component.local_transform.affine_matrix();
        component.dirty = false;
    }
    const std::vector<Entity> children = component.children;
    for (const Entity child : children) {
        update_world_matrix(child, component.world_affine_matrix);
    }
}

template class BasicTransformEditQueue<Transform2D>;
template class BasicTransformEditQueue<Transform3D>;
template class BasicTransformReadView<Transform2D, math::Mat3, TransformInstance2D>;
template class BasicTransformReadView<Transform3D, math::Mat4, TransformInstance3D>;
template class BasicTransformManager<Transform2D>;
template class BasicTransformManager<Transform3D>;

} // namespace cubey
