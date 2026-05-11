#pragma once

#include <cubey/detail/stable_slot_store.h>
#include <cubey/entity.h>
#include <cubey/math.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_registry.h>
#include <cubey/transform_manager.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cubey {

struct Bounds3D {
    math::Vec3 center{0.0F, 0.0F, 0.0F};
    math::Vec3 half_extent{0.0F, 0.0F, 0.0F};
};

struct RenderablePrimitive3D {
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    std::uint32_t instance_count = 1;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};

struct Renderable3D {
    std::vector<RenderablePrimitive3D> primitives{};
    Bounds3D local_bounds{};
    bool visible = true;
    bool cast_shadows = true;
    bool receive_shadows = true;
};

struct RenderablePacket3D {
    Entity entity{};
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    math::Mat4 world_affine_matrix{1.0F};
    Bounds3D local_bounds{};
    bool cast_shadows = true;
    bool receive_shadows = true;
    std::uint32_t instance_count = 1;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};

struct Renderable3DManagerTag {};

template <typename Tag> struct RenderableInstance {
    detail::StableSlotId slot{};

    [[nodiscard]] bool is_null() const noexcept {
        return slot.is_null();
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(RenderableInstance lhs, RenderableInstance rhs) = default;
};

using RenderableInstance3D = RenderableInstance<Renderable3DManagerTag>;

class RenderableManager3D;

class RenderableEditQueue3D {
  public:
    void create(Entity entity, const Renderable3D& renderable) {
        creates_.push_back(RenderableEdit{
            .entity = entity,
            .renderable = renderable,
        });
    }

    void destroy(Entity entity) {
        destroys_.push_back(entity);
    }

    void set_renderable(Entity entity, const Renderable3D& renderable) {
        updates_.push_back(RenderableEdit{
            .entity = entity,
            .renderable = renderable,
        });
    }

  private:
    friend class RenderableManager3D;

    struct RenderableEdit {
        Entity entity{};
        Renderable3D renderable{};
    };

    std::vector<RenderableEdit> creates_{};
    std::vector<Entity> destroys_{};
    std::vector<RenderableEdit> updates_{};
};

class RenderableReadView3D {
  public:
    struct Component {
        Entity entity{};
        Renderable3D renderable{};
    };

    struct Snapshot {
        std::vector<Component> components{};
        std::vector<RenderableInstance3D> active_instances{};
        std::unordered_map<Entity, std::size_t, EntityHash> entity_to_component{};
        std::unordered_map<detail::StableSlotId, std::size_t, detail::StableSlotIdHash>
            slot_to_component{};
    };

    RenderableReadView3D() = default;

    explicit RenderableReadView3D(std::shared_ptr<const Snapshot> snapshot)
        : snapshot_(std::move(snapshot)) {
        if (snapshot_ == nullptr) {
            snapshot_ = std::make_shared<Snapshot>();
        }
    }

    [[nodiscard]] RenderableInstance3D instance(Entity entity) const {
        const auto position = snapshot_->entity_to_component.find(entity);
        if (position == snapshot_->entity_to_component.end()) {
            throw std::runtime_error("entity does not have a renderable component");
        }
        return snapshot_->active_instances[position->second];
    }

    [[nodiscard]] Entity entity(RenderableInstance3D instance) const {
        return component(instance).entity;
    }

    [[nodiscard]] const Renderable3D& renderable(RenderableInstance3D instance) const {
        return component(instance).renderable;
    }

    [[nodiscard]] const std::vector<RenderableInstance3D>& active_instances() const noexcept {
        return snapshot_->active_instances;
    }

  private:
    [[nodiscard]] const Component& component(RenderableInstance3D instance) const {
        const auto position = snapshot_->slot_to_component.find(instance.slot);
        if (position == snapshot_->slot_to_component.end()) {
            throw std::runtime_error("renderable instance is not part of this read view");
        }
        return snapshot_->components[position->second];
    }

    std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<Snapshot>();
};

class RenderableManager3D {
  public:
    using Instance = RenderableInstance3D;
    using ReadView = RenderableReadView3D;
    using SnapshotComponent = RenderableReadView3D::Component;
    using Snapshot = RenderableReadView3D::Snapshot;

    [[nodiscard]] bool has_component(Entity entity) const {
        return entity_to_slot_.contains(entity);
    }

    [[nodiscard]] Instance instance(Entity entity) const {
        return Instance{.slot = slot_for(entity)};
    }

    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const {
        return snapshot_;
    }

    void validate(const RenderableEditQueue3D& edits, const EntityManager& entities,
                  const render::RenderResourceRegistry* resources = nullptr) const {
        std::unordered_set<Entity, EntityHash> existing{};
        for (const detail::StableSlotId slot_id : slots_.active_instances()) {
            existing.insert(slots_.get(slot_id).entity);
        }

        for (const auto& create : edits.creates_) {
            if (!create.entity || !entities.is_current(create.entity)) {
                throw std::runtime_error("renderable create requires a current entity");
            }
            if (existing.contains(create.entity)) {
                throw std::runtime_error("entity already has a renderable component");
            }
            validate_renderable(create.renderable, resources);
            existing.insert(create.entity);
        }

        for (const auto& update : edits.updates_) {
            if (!existing.contains(update.entity)) {
                throw std::runtime_error("renderable update requires an existing component");
            }
            validate_renderable(update.renderable, resources);
        }

        for (const Entity entity : edits.destroys_) {
            if (!existing.contains(entity)) {
                throw std::runtime_error("renderable destroy requires an existing component");
            }
            existing.erase(entity);
        }
    }

    void apply(const RenderableEditQueue3D& edits, std::uint64_t retire_epoch) {
        for (const auto& create : edits.creates_) {
            const detail::StableSlotId slot_id = slots_.create(Component{
                .entity = create.entity,
                .renderable = create.renderable,
            });
            entity_to_slot_[create.entity] = slot_id;
        }

        for (const auto& update : edits.updates_) {
            component_for(update.entity).renderable = update.renderable;
        }

        for (const Entity entity : edits.destroys_) {
            destroy_entity_if_exists(entity, retire_epoch);
        }
    }

    void destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch) {
        const auto position = entity_to_slot_.find(entity);
        if (position == entity_to_slot_.end()) {
            return;
        }
        slots_.destroy(position->second, retire_epoch);
        entity_to_slot_.erase(position);
    }

    void publish_snapshot() {
        auto next_snapshot = std::make_shared<Snapshot>();
        for (const detail::StableSlotId slot_id : slots_.active_instances()) {
            const Component& component = slots_.get(slot_id);
            const std::size_t index = next_snapshot->components.size();
            next_snapshot->components.push_back(SnapshotComponent{
                .entity = component.entity,
                .renderable = component.renderable,
            });
            next_snapshot->active_instances.push_back(Instance{.slot = slot_id});
            next_snapshot->entity_to_component[component.entity] = index;
            next_snapshot->slot_to_component[slot_id] = index;
        }
        snapshot_ = std::move(next_snapshot);
    }

    void retire_destroyed_up_to(std::uint64_t epoch) {
        static_cast<void>(slots_.retire_destroyed_up_to(epoch));
    }

  private:
    struct Component {
        Entity entity{};
        Renderable3D renderable{};
    };

    static void validate_renderable(const Renderable3D& renderable,
                                    const render::RenderResourceRegistry* resources) {
        if (renderable.primitives.empty()) {
            throw std::runtime_error("renderable requires at least one primitive");
        }
        for (const RenderablePrimitive3D& primitive : renderable.primitives) {
            if (!primitive.mesh) {
                throw std::runtime_error("renderable primitive requires a mesh handle");
            }
            if (!primitive.material) {
                throw std::runtime_error("renderable primitive requires a material handle");
            }
            if (resources != nullptr && !resources->is_alive(primitive.mesh)) {
                throw std::runtime_error("renderable primitive mesh handle is not alive");
            }
            if (resources != nullptr && !resources->is_alive(primitive.material)) {
                throw std::runtime_error("renderable primitive material handle is not alive");
            }
            if (primitive.instance_count == 0) {
                throw std::runtime_error("renderable primitive instance count must be positive");
            }
        }
    }

    [[nodiscard]] detail::StableSlotId slot_for(Entity entity) const {
        const auto position = entity_to_slot_.find(entity);
        if (position == entity_to_slot_.end()) {
            throw std::runtime_error("entity does not have a renderable component");
        }
        return position->second;
    }

    [[nodiscard]] Component& component_for(Entity entity) {
        return slots_.get(slot_for(entity));
    }

    detail::StableSlotStore<Component> slots_{};
    std::unordered_map<Entity, detail::StableSlotId, EntityHash> entity_to_slot_{};
    std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<Snapshot>();
};

[[nodiscard]] inline std::vector<RenderablePacket3D>
build_renderable_packets_3d(const RenderableReadView3D& renderables,
                            const TransformReadView3D& transforms) {
    std::vector<RenderablePacket3D> packets;
    for (const RenderableInstance3D instance : renderables.active_instances()) {
        const Renderable3D& renderable = renderables.renderable(instance);
        if (!renderable.visible) {
            continue;
        }

        const Entity entity = renderables.entity(instance);
        const math::Mat4& world = transforms.world_affine_matrix(transforms.instance(entity));
        packets.reserve(packets.size() + renderable.primitives.size());
        for (const RenderablePrimitive3D& primitive : renderable.primitives) {
            packets.push_back(RenderablePacket3D{
                .entity = entity,
                .mesh = primitive.mesh,
                .material = primitive.material,
                .world_affine_matrix = world,
                .local_bounds = renderable.local_bounds,
                .cast_shadows = renderable.cast_shadows,
                .receive_shadows = renderable.receive_shadows,
                .instance_count = primitive.instance_count,
                .first_index = primitive.first_index,
                .vertex_offset = primitive.vertex_offset,
                .first_instance = primitive.first_instance,
            });
        }
    }
    return packets;
}

} // namespace cubey
