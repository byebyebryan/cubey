#pragma once

#include <cubey/core/math.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/stable_slot_store.h>
#include <cubey/scene/transform_2d.h>
#include <cubey/scene/transform_3d.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace cubey {

struct Transform2DManagerTag {};
struct Transform3DManagerTag {};

template <typename Tag> struct TransformInstance {
    StableSlotId slot{};

    [[nodiscard]] bool is_null() const noexcept {
        return slot.is_null();
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(TransformInstance lhs, TransformInstance rhs) = default;
};

using TransformInstance2D = TransformInstance<Transform2DManagerTag>;
using TransformInstance3D = TransformInstance<Transform3DManagerTag>;

template <typename TransformT> class BasicTransformManager;

template <typename TransformT> class BasicTransformEditQueue {
  public:
    void create(Entity entity, const TransformT& local_transform = TransformT{},
                Entity parent = {});
    void destroy(Entity entity);
    void set_local_transform(Entity entity, const TransformT& local_transform);
    void set_parent(Entity child, Entity parent);
    void clear_parent(Entity child);

  private:
    friend class BasicTransformManager<TransformT>;

    struct CreateEdit {
        Entity entity{};
        TransformT local_transform{};
        Entity parent{};
    };

    struct LocalUpdateEdit {
        Entity entity{};
        TransformT local_transform{};
    };

    struct ParentUpdateEdit {
        Entity child{};
        Entity parent{};
    };

    std::vector<CreateEdit> creates_{};
    std::vector<Entity> destroys_{};
    std::vector<LocalUpdateEdit> local_updates_{};
    std::vector<ParentUpdateEdit> parent_updates_{};
};

using TransformEditQueue2D = BasicTransformEditQueue<Transform2D>;
using TransformEditQueue3D = BasicTransformEditQueue<Transform3D>;

template <typename TransformT, typename MatrixT, typename InstanceT> class BasicTransformReadView {
  public:
    struct Component {
        Entity entity{};
        TransformT local_transform{};
        MatrixT world_affine_matrix{1.0F};
        Entity parent{};
    };

    struct Snapshot {
        std::vector<Component> components{};
        std::vector<InstanceT> active_instances{};
        std::unordered_map<Entity, std::size_t, EntityHash> entity_to_component{};
        std::unordered_map<StableSlotId, std::size_t, StableSlotIdHash> slot_to_component{};
    };

    BasicTransformReadView();
    explicit BasicTransformReadView(std::shared_ptr<const Snapshot> snapshot);

    [[nodiscard]] InstanceT instance(Entity entity) const;
    [[nodiscard]] const TransformT& local_transform(InstanceT instance) const;
    [[nodiscard]] const MatrixT& world_affine_matrix(InstanceT instance) const;
    [[nodiscard]] Entity parent(InstanceT instance) const;
    [[nodiscard]] const std::vector<InstanceT>& active_instances() const noexcept;

  private:
    [[nodiscard]] const Component& component(InstanceT instance) const;

    std::shared_ptr<const Snapshot> snapshot_;
};

using TransformReadView2D = BasicTransformReadView<Transform2D, math::Mat3, TransformInstance2D>;
using TransformReadView3D = BasicTransformReadView<Transform3D, math::Mat4, TransformInstance3D>;

template <typename TransformT> struct TransformTraits;

template <> struct TransformTraits<Transform2D> {
    using Matrix = math::Mat3;
    using Instance = TransformInstance2D;
    using ReadView = TransformReadView2D;
};

template <> struct TransformTraits<Transform3D> {
    using Matrix = math::Mat4;
    using Instance = TransformInstance3D;
    using ReadView = TransformReadView3D;
};

template <typename TransformT> class BasicTransformManager {
  public:
    using Matrix = typename TransformTraits<TransformT>::Matrix;
    using Instance = typename TransformTraits<TransformT>::Instance;
    using ReadView = typename TransformTraits<TransformT>::ReadView;
    using SnapshotComponent = typename ReadView::Component;
    using Snapshot = typename ReadView::Snapshot;

    [[nodiscard]] bool has_component(Entity entity) const;
    [[nodiscard]] Instance instance(Entity entity) const;
    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const;

    void validate(const BasicTransformEditQueue<TransformT>& edits,
                  const EntityManager& entities) const;
    void validate_destroy_entity(Entity entity) const;
    void apply(const BasicTransformEditQueue<TransformT>& edits, std::uint64_t retire_epoch);
    void destroy_entity_if_exists(Entity entity, std::uint64_t retire_epoch);
    void update_world_matrices();
    void publish_snapshot();
    void retire_destroyed_up_to(std::uint64_t epoch);

  private:
    struct Component {
        Entity entity{};
        TransformT local_transform{};
        Matrix world_affine_matrix{1.0F};
        Entity parent{};
        std::vector<Entity> children{};
        bool dirty = true;
    };

    [[nodiscard]] static bool
    has_child_in(Entity entity, const std::unordered_map<Entity, Entity, EntityHash>& parent_of);
    static void reject_cycles(const std::unordered_map<Entity, Entity, EntityHash>& parent_of);

    [[nodiscard]] StableSlotId slot_for(Entity entity) const;
    [[nodiscard]] Component& component_for(Entity entity);
    [[nodiscard]] const Component& component_for(Entity entity) const;
    void attach_to_parent(Entity child, Entity parent);
    void detach_from_parent(Entity child, Entity parent);
    void mark_subtree_dirty(Entity entity);
    void update_world_matrix(Entity entity, const Matrix& parent_world);

    StableSlotStore<Component> slots_{};
    std::unordered_map<Entity, StableSlotId, EntityHash> entity_to_slot_{};
    std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<Snapshot>();
};

using TransformManager2D = BasicTransformManager<Transform2D>;
using TransformManager3D = BasicTransformManager<Transform3D>;

extern template class BasicTransformEditQueue<Transform2D>;
extern template class BasicTransformEditQueue<Transform3D>;
extern template class BasicTransformReadView<Transform2D, math::Mat3, TransformInstance2D>;
extern template class BasicTransformReadView<Transform3D, math::Mat4, TransformInstance3D>;
extern template class BasicTransformManager<Transform2D>;
extern template class BasicTransformManager<Transform3D>;

} // namespace cubey
