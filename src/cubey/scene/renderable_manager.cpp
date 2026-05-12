#include <cubey/scene/renderable_manager.h>

#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace cubey {

void RenderableEditQueue3D::create(Entity entity, const Renderable3D& renderable) {
    creates_.push_back(RenderableEdit{
        .entity = entity,
        .renderable = renderable,
    });
}

void RenderableEditQueue3D::destroy(Entity entity) {
    destroys_.push_back(entity);
}

void RenderableEditQueue3D::set_renderable(Entity entity, const Renderable3D& renderable) {
    updates_.push_back(RenderableEdit{
        .entity = entity,
        .renderable = renderable,
    });
}

RenderableReadView3D::RenderableReadView3D(std::shared_ptr<const Snapshot> snapshot)
    : snapshot_(std::move(snapshot)) {
    if (snapshot_ == nullptr) {
        snapshot_ = std::make_shared<Snapshot>();
    }
}

RenderableInstance3D RenderableReadView3D::instance(Entity entity) const {
    const auto position = snapshot_->entity_to_component.find(entity);
    if (position == snapshot_->entity_to_component.end()) {
        throw std::runtime_error("entity does not have a renderable component");
    }
    return snapshot_->active_instances[position->second];
}

Entity RenderableReadView3D::entity(RenderableInstance3D instance) const {
    return component(instance).entity;
}

const Renderable3D& RenderableReadView3D::renderable(RenderableInstance3D instance) const {
    return component(instance).renderable;
}

const std::vector<RenderableInstance3D>& RenderableReadView3D::active_instances() const noexcept {
    return snapshot_->active_instances;
}

const RenderableReadView3D::Component&
RenderableReadView3D::component(RenderableInstance3D instance) const {
    const auto position = snapshot_->slot_to_component.find(instance.slot);
    if (position == snapshot_->slot_to_component.end()) {
        throw std::runtime_error("renderable instance is not part of this read view");
    }
    return snapshot_->components[position->second];
}

bool RenderableManager3D::has_component(Entity entity) const {
    return store_.has_component(entity);
}

RenderableManager3D::Instance RenderableManager3D::instance(Entity entity) const {
    return store_.instance(entity);
}

std::shared_ptr<const RenderableManager3D::Snapshot> RenderableManager3D::snapshot() const {
    return store_.snapshot();
}

void RenderableManager3D::validate(const RenderableEditQueue3D& edits,
                                   const EntityManager& entities,
                                   const render::RenderResourceRegistry* resources) const {
    std::unordered_set<Entity, EntityHash> existing{};
    for (const Entity entity : store_.active_entities()) {
        existing.insert(entity);
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

void RenderableManager3D::apply(const RenderableEditQueue3D& edits, std::uint64_t retire_epoch) {
    for (const auto& create : edits.creates_) {
        store_.create(create.entity, Component{
                                         .entity = create.entity,
                                         .renderable = create.renderable,
                                     });
    }

    for (const auto& update : edits.updates_) {
        store_.component_for(update.entity).renderable = update.renderable;
    }

    for (const Entity entity : edits.destroys_) {
        destroy_entity_if_exists(entity, retire_epoch);
    }
}

void RenderableManager3D::destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch) {
    store_.destroy_entity_if_exists(entity, retire_epoch);
}

void RenderableManager3D::publish_snapshot() {
    store_.publish_snapshot([](const Component& component) {
        return SnapshotComponent{
            .entity = component.entity,
            .renderable = component.renderable,
        };
    });
}

void RenderableManager3D::retire_destroyed_up_to(std::uint64_t epoch) {
    store_.retire_destroyed_up_to(epoch);
}

void RenderableManager3D::validate_renderable(const Renderable3D& renderable,
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

Bounds3D transform_bounds_3d(const Bounds3D& bounds, const math::Mat4& transform) {
    const math::Vec4 center = transform * math::Vec4{bounds.center, 1.0F};
    const math::Vec3 half_extent{
        (std::fabs(transform[0][0]) * bounds.half_extent.x) +
            (std::fabs(transform[1][0]) * bounds.half_extent.y) +
            (std::fabs(transform[2][0]) * bounds.half_extent.z),
        (std::fabs(transform[0][1]) * bounds.half_extent.x) +
            (std::fabs(transform[1][1]) * bounds.half_extent.y) +
            (std::fabs(transform[2][1]) * bounds.half_extent.z),
        (std::fabs(transform[0][2]) * bounds.half_extent.x) +
            (std::fabs(transform[1][2]) * bounds.half_extent.y) +
            (std::fabs(transform[2][2]) * bounds.half_extent.z),
    };
    return Bounds3D{
        .center = {center.x, center.y, center.z},
        .half_extent = half_extent,
    };
}

std::vector<RenderablePacket3D> build_renderable_packets_3d(const RenderableReadView3D& renderables,
                                                            const TransformReadView3D& transforms) {
    std::vector<RenderablePacket3D> packets;
    for (const RenderableInstance3D instance : renderables.active_instances()) {
        const Renderable3D& renderable = renderables.renderable(instance);
        if (!renderable.visible) {
            continue;
        }

        const Entity entity = renderables.entity(instance);
        const math::Mat4& world = transforms.world_affine_matrix(transforms.instance(entity));
        const Bounds3D world_bounds = transform_bounds_3d(renderable.local_bounds, world);
        packets.reserve(packets.size() + renderable.primitives.size());
        for (const RenderablePrimitive3D& primitive : renderable.primitives) {
            packets.push_back(RenderablePacket3D{
                .entity = entity,
                .mesh = primitive.mesh,
                .material = primitive.material,
                .world_affine_matrix = world,
                .local_bounds = renderable.local_bounds,
                .world_bounds = world_bounds,
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
