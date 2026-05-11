#pragma once

#include <cubey/detail/single_instance_component_store.h>
#include <cubey/detail/stable_slot_store.h>
#include <cubey/entity.h>
#include <cubey/math.h>
#include <cubey/transform_manager.h>

#include <glm/geometric.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cubey {

enum class LightKind3D : std::uint8_t {
    Directional,
    Point,
};

struct Light3D {
    LightKind3D kind = LightKind3D::Directional;
    math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    math::Vec3 direction{0.0F, -1.0F, 0.0F};
    float range = 1.0F;
    bool visible = true;
    bool casts_shadows = false;
};

[[nodiscard]] inline Light3D directional_light_3d(math::Vec3 direction,
                                                  math::Vec3 color = {1.0F, 1.0F, 1.0F},
                                                  float intensity = 1.0F) {
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

[[nodiscard]] inline Light3D point_light_3d(math::Vec3 color = {1.0F, 1.0F, 1.0F},
                                            float intensity = 1.0F, float range = 1.0F) {
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

struct LightPacket3D {
    Entity entity{};
    LightKind3D kind = LightKind3D::Directional;
    math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    math::Vec3 direction{0.0F, -1.0F, 0.0F};
    math::Vec3 position{0.0F, 0.0F, 0.0F};
    float range = 1.0F;
    bool casts_shadows = false;
};

struct Light3DManagerTag {};

template <typename Tag> struct LightInstance {
    detail::StableSlotId slot{};

    [[nodiscard]] bool is_null() const noexcept {
        return slot.is_null();
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(LightInstance lhs, LightInstance rhs) = default;
};

using LightInstance3D = LightInstance<Light3DManagerTag>;

class LightManager3D;

class LightEditQueue3D {
  public:
    void create(Entity entity, const Light3D& light) {
        creates_.push_back(LightEdit{
            .entity = entity,
            .light = light,
        });
    }

    void destroy(Entity entity) {
        destroys_.push_back(entity);
    }

    void set_light(Entity entity, const Light3D& light) {
        updates_.push_back(LightEdit{
            .entity = entity,
            .light = light,
        });
    }

  private:
    friend class LightManager3D;

    struct LightEdit {
        Entity entity{};
        Light3D light{};
    };

    std::vector<LightEdit> creates_{};
    std::vector<Entity> destroys_{};
    std::vector<LightEdit> updates_{};
};

class LightReadView3D {
  public:
    struct Component {
        Entity entity{};
        Light3D light{};
    };

    struct Snapshot {
        std::vector<Component> components{};
        std::vector<LightInstance3D> active_instances{};
        std::unordered_map<Entity, std::size_t, EntityHash> entity_to_component{};
        std::unordered_map<detail::StableSlotId, std::size_t, detail::StableSlotIdHash>
            slot_to_component{};
    };

    LightReadView3D() = default;

    explicit LightReadView3D(std::shared_ptr<const Snapshot> snapshot)
        : snapshot_(std::move(snapshot)) {
        if (snapshot_ == nullptr) {
            snapshot_ = std::make_shared<Snapshot>();
        }
    }

    [[nodiscard]] LightInstance3D instance(Entity entity) const {
        const auto position = snapshot_->entity_to_component.find(entity);
        if (position == snapshot_->entity_to_component.end()) {
            throw std::runtime_error("entity does not have a light component");
        }
        return snapshot_->active_instances[position->second];
    }

    [[nodiscard]] Entity entity(LightInstance3D instance) const {
        return component(instance).entity;
    }

    [[nodiscard]] const Light3D& light(LightInstance3D instance) const {
        return component(instance).light;
    }

    [[nodiscard]] const std::vector<LightInstance3D>& active_instances() const noexcept {
        return snapshot_->active_instances;
    }

  private:
    [[nodiscard]] const Component& component(LightInstance3D instance) const {
        const auto position = snapshot_->slot_to_component.find(instance.slot);
        if (position == snapshot_->slot_to_component.end()) {
            throw std::runtime_error("light instance is not part of this read view");
        }
        return snapshot_->components[position->second];
    }

    std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<Snapshot>();
};

class LightManager3D {
  public:
    using Instance = LightInstance3D;
    using ReadView = LightReadView3D;
    using SnapshotComponent = LightReadView3D::Component;
    using Snapshot = LightReadView3D::Snapshot;

  private:
    struct Component {
        Entity entity{};
        Light3D light{};
    };
    using Store = detail::SingleInstanceComponentStore<Component, Instance, Snapshot>;

  public:
    [[nodiscard]] bool has_component(Entity entity) const {
        return store_.has_component(entity);
    }

    [[nodiscard]] Instance instance(Entity entity) const {
        return store_.instance(entity);
    }

    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const {
        return store_.snapshot();
    }

    void validate(const LightEditQueue3D& edits, const EntityManager& entities) const {
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

    void apply(const LightEditQueue3D& edits, std::uint64_t retire_epoch) {
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

    void destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch) {
        store_.destroy_entity_if_exists(entity, retire_epoch);
    }

    void publish_snapshot() {
        store_.publish_snapshot([](const Component& component) {
            return SnapshotComponent{
                .entity = component.entity,
                .light = component.light,
            };
        });
    }

    void retire_destroyed_up_to(std::uint64_t epoch) {
        store_.retire_destroyed_up_to(epoch);
    }

  private:
    static void validate_light(const Light3D& light) {
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

    Store store_{};
};

[[nodiscard]] inline std::vector<LightPacket3D>
build_light_packets_3d(const LightReadView3D& lights, const TransformReadView3D& transforms) {
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
