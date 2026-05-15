#pragma once

#include <cubey/core/math.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_registry.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/single_instance_component_store.h>
#include <cubey/scene/stable_slot_store.h>
#include <cubey/scene/transform_manager.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
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
    bool culling_enabled = true;
    bool cast_shadows = true;
    bool receive_shadows = true;
};

struct RenderablePacket3D {
    Entity entity{};
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    math::Mat4 world_affine_matrix{1.0F};
    Bounds3D local_bounds{};
    Bounds3D world_bounds{};
    bool culling_enabled = true;
    bool cast_shadows = true;
    bool receive_shadows = true;
    std::uint32_t instance_count = 1;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};

struct Renderable3DManagerTag {};

template <typename Tag> struct RenderableInstance {
    StableSlotId slot{};

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
    void create(Entity entity, const Renderable3D& renderable);
    void destroy(Entity entity);
    void set_renderable(Entity entity, const Renderable3D& renderable);

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
        std::unordered_map<StableSlotId, std::size_t, StableSlotIdHash> slot_to_component{};
    };

    RenderableReadView3D() = default;

    explicit RenderableReadView3D(std::shared_ptr<const Snapshot> snapshot);

    [[nodiscard]] RenderableInstance3D instance(Entity entity) const;
    [[nodiscard]] Entity entity(RenderableInstance3D instance) const;
    [[nodiscard]] const Renderable3D& renderable(RenderableInstance3D instance) const;
    [[nodiscard]] const std::vector<RenderableInstance3D>& active_instances() const noexcept;

  private:
    [[nodiscard]] const Component& component(RenderableInstance3D instance) const;

    std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<Snapshot>();
};

class RenderableManager3D {
  public:
    using Instance = RenderableInstance3D;
    using ReadView = RenderableReadView3D;
    using SnapshotComponent = RenderableReadView3D::Component;
    using Snapshot = RenderableReadView3D::Snapshot;

  private:
    struct Component {
        Entity entity{};
        Renderable3D renderable{};
    };
    using Store = SingleInstanceComponentStore<Component, Instance, Snapshot>;

  public:
    [[nodiscard]] bool has_component(Entity entity) const;
    [[nodiscard]] Instance instance(Entity entity) const;
    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const;
    void validate(const RenderableEditQueue3D& edits, const EntityManager& entities,
                  const render::RenderResourceRegistry* resources = nullptr) const;
    void apply(const RenderableEditQueue3D& edits, std::uint64_t retire_epoch);
    void destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch);
    void publish_snapshot();
    void retire_destroyed_up_to(std::uint64_t epoch);

  private:
    static void validate_renderable(const Renderable3D& renderable,
                                    const render::RenderResourceRegistry* resources);

    Store store_{};
};

[[nodiscard]] Bounds3D transform_bounds_3d(const Bounds3D& bounds, const math::Mat4& transform);

[[nodiscard]] std::vector<RenderablePacket3D>
build_renderable_packets_3d(const RenderableReadView3D& renderables,
                            const TransformReadView3D& transforms);

} // namespace cubey
