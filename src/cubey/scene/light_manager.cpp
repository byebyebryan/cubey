#include <cubey/scene/light_manager.h>

#include <glm/geometric.hpp>

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace cubey {

Light3D directional_light_3d(math::Vec3 direction, math::Vec3 color, float intensity) {
    return Light3D{
        .kind = LightKind3D::Directional,
        .color = color,
        .intensity = intensity,
        .direction = direction,
        .range = 1.0F,
        .visible = true,
        .casts_shadows = false,
    };
}

Light3D point_light_3d(math::Vec3 color, float intensity, float range) {
    return Light3D{
        .kind = LightKind3D::Point,
        .color = color,
        .intensity = intensity,
        .direction = {0.0F, -1.0F, 0.0F},
        .range = range,
        .visible = true,
        .casts_shadows = false,
    };
}

void LightEditQueue3D::create(Entity entity, const Light3D& light) {
    creates_.push_back(LightEdit{
        .entity = entity,
        .light = light,
    });
}

void LightEditQueue3D::destroy(Entity entity) {
    destroys_.push_back(entity);
}

void LightEditQueue3D::set_light(Entity entity, const Light3D& light) {
    updates_.push_back(LightEdit{
        .entity = entity,
        .light = light,
    });
}

LightReadView3D::LightReadView3D(std::shared_ptr<const Snapshot> snapshot)
    : snapshot_(std::move(snapshot)) {
    if (snapshot_ == nullptr) {
        snapshot_ = std::make_shared<Snapshot>();
    }
}

LightInstance3D LightReadView3D::instance(Entity entity) const {
    const auto position = snapshot_->entity_to_component.find(entity);
    if (position == snapshot_->entity_to_component.end()) {
        throw std::runtime_error("entity does not have a light component");
    }
    return snapshot_->active_instances[position->second];
}

Entity LightReadView3D::entity(LightInstance3D instance) const {
    return component(instance).entity;
}

const Light3D& LightReadView3D::light(LightInstance3D instance) const {
    return component(instance).light;
}

const std::vector<LightInstance3D>& LightReadView3D::active_instances() const noexcept {
    return snapshot_->active_instances;
}

const LightReadView3D::Component& LightReadView3D::component(LightInstance3D instance) const {
    const auto position = snapshot_->slot_to_component.find(instance.slot);
    if (position == snapshot_->slot_to_component.end()) {
        throw std::runtime_error("light instance is not part of this read view");
    }
    return snapshot_->components[position->second];
}

bool LightManager3D::has_component(Entity entity) const {
    return store_.has_component(entity);
}

LightManager3D::Instance LightManager3D::instance(Entity entity) const {
    return store_.instance(entity);
}

std::shared_ptr<const LightManager3D::Snapshot> LightManager3D::snapshot() const {
    return store_.snapshot();
}

void LightManager3D::validate(const LightEditQueue3D& edits, const EntityManager& entities) const {
    std::unordered_set<Entity, EntityHash> existing{};
    for (const Entity entity : store_.active_entities()) {
        existing.insert(entity);
    }

    for (const auto& create : edits.creates_) {
        if (!create.entity || !entities.is_current(create.entity)) {
            throw std::runtime_error("light create requires a current entity");
        }
        if (existing.contains(create.entity)) {
            throw std::runtime_error("entity already has a light component");
        }
        validate_light(create.light);
        existing.insert(create.entity);
    }

    for (const auto& update : edits.updates_) {
        if (!existing.contains(update.entity)) {
            throw std::runtime_error("light update requires an existing component");
        }
        validate_light(update.light);
    }

    for (const Entity entity : edits.destroys_) {
        if (!existing.contains(entity)) {
            throw std::runtime_error("light destroy requires an existing component");
        }
        existing.erase(entity);
    }
}

void LightManager3D::apply(const LightEditQueue3D& edits, std::uint64_t retire_epoch) {
    for (const auto& create : edits.creates_) {
        store_.create(create.entity, Component{
                                         .entity = create.entity,
                                         .light = create.light,
                                     });
    }

    for (const auto& update : edits.updates_) {
        store_.component_for(update.entity).light = update.light;
    }

    for (const Entity entity : edits.destroys_) {
        destroy_entity_if_exists(entity, retire_epoch);
    }
}

void LightManager3D::destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch) {
    store_.destroy_entity_if_exists(entity, retire_epoch);
}

void LightManager3D::publish_snapshot() {
    store_.publish_snapshot([](const Component& component) {
        return SnapshotComponent{
            .entity = component.entity,
            .light = component.light,
        };
    });
}

void LightManager3D::retire_destroyed_up_to(std::uint64_t epoch) {
    store_.retire_destroyed_up_to(epoch);
}

void LightManager3D::validate_light(const Light3D& light) {
    if (light.color.x < 0.0F || light.color.y < 0.0F || light.color.z < 0.0F) {
        throw std::runtime_error("light color channels must be non-negative");
    }
    if (light.intensity < 0.0F) {
        throw std::runtime_error("light intensity must be non-negative");
    }

    switch (light.kind) {
    case LightKind3D::Directional:
        if (glm::length(light.direction) <= 0.0F) {
            throw std::runtime_error("directional light requires a nonzero direction");
        }
        break;
    case LightKind3D::Point:
        if (light.range <= 0.0F) {
            throw std::runtime_error("point light range must be positive");
        }
        break;
    default:
        throw std::runtime_error("light kind is not supported");
    }
}

std::vector<LightPacket3D> build_light_packets_3d(const LightReadView3D& lights,
                                                  const TransformReadView3D& transforms) {
    std::vector<LightPacket3D> packets;
    for (const LightInstance3D instance : lights.active_instances()) {
        const Light3D& light = lights.light(instance);
        if (!light.visible) {
            continue;
        }

        const Entity entity = lights.entity(instance);
        LightPacket3D packet{
            .entity = entity,
            .kind = light.kind,
            .color = light.color,
            .intensity = light.intensity,
            .direction = light.direction,
            .position = {0.0F, 0.0F, 0.0F},
            .range = light.range,
            .casts_shadows = light.casts_shadows,
        };

        switch (light.kind) {
        case LightKind3D::Directional:
            packet.direction = glm::normalize(light.direction);
            break;
        case LightKind3D::Point: {
            const math::Vec4 position =
                transforms.world_affine_matrix(transforms.instance(entity)) *
                math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
            packet.position = {position.x, position.y, position.z};
            break;
        }
        default:
            throw std::runtime_error("light kind is not supported");
        }

        packets.push_back(packet);
    }
    return packets;
}

} // namespace cubey
