#pragma once

#include <cubey/core/math.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/single_instance_component_store.h>
#include <cubey/scene/stable_slot_store.h>
#include <cubey/scene/transform_manager.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
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

[[nodiscard]] Light3D directional_light_3d(math::Vec3 direction,
                                           math::Vec3 color = {1.0F, 1.0F, 1.0F},
                                           float intensity = 1.0F);

[[nodiscard]] Light3D point_light_3d(math::Vec3 color = {1.0F, 1.0F, 1.0F}, float intensity = 1.0F,
                                     float range = 1.0F);

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
    StableSlotId slot{};

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
    void create(Entity entity, const Light3D& light);
    void destroy(Entity entity);
    void set_light(Entity entity, const Light3D& light);

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
        std::unordered_map<StableSlotId, std::size_t, StableSlotIdHash> slot_to_component{};
    };

    LightReadView3D() = default;

    explicit LightReadView3D(std::shared_ptr<const Snapshot> snapshot);

    [[nodiscard]] LightInstance3D instance(Entity entity) const;
    [[nodiscard]] Entity entity(LightInstance3D instance) const;
    [[nodiscard]] const Light3D& light(LightInstance3D instance) const;
    [[nodiscard]] const std::vector<LightInstance3D>& active_instances() const noexcept;

  private:
    [[nodiscard]] const Component& component(LightInstance3D instance) const;

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
    using Store = SingleInstanceComponentStore<Component, Instance, Snapshot>;

  public:
    [[nodiscard]] bool has_component(Entity entity) const;
    [[nodiscard]] Instance instance(Entity entity) const;
    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const;
    void validate(const LightEditQueue3D& edits, const EntityManager& entities) const;
    void apply(const LightEditQueue3D& edits, std::uint64_t retire_epoch);
    void destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch);
    void publish_snapshot();
    void retire_destroyed_up_to(std::uint64_t epoch);

  private:
    static void validate_light(const Light3D& light);

    Store store_{};
};

[[nodiscard]] std::vector<LightPacket3D>
build_light_packets_3d(const LightReadView3D& lights, const TransformReadView3D& transforms);

} // namespace cubey
