#pragma once

#include <cubey/camera_2d.h>
#include <cubey/camera_3d.h>
#include <cubey/detail/stable_slot_store.h>
#include <cubey/entity.h>
#include <cubey/transform_manager.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cubey {

struct Camera2DManagerTag {};
struct Camera3DManagerTag {};

template <typename Tag> struct CameraInstance {
    detail::StableSlotId slot{};

    [[nodiscard]] bool is_null() const noexcept {
        return slot.is_null();
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(CameraInstance lhs, CameraInstance rhs) = default;
};

using CameraInstance2D = CameraInstance<Camera2DManagerTag>;
using CameraInstance3D = CameraInstance<Camera3DManagerTag>;

template <typename CameraT> class BasicCameraManager;

template <typename CameraT> class BasicCameraEditQueue {
  public:
    void create(Entity entity, const CameraT& camera = CameraT{}) {
        creates_.push_back(CameraEdit{
            .entity = entity,
            .camera = camera,
        });
    }

    void destroy(Entity entity) {
        destroys_.push_back(entity);
    }

    void set_camera(Entity entity, const CameraT& camera) {
        updates_.push_back(CameraEdit{
            .entity = entity,
            .camera = camera,
        });
    }

  private:
    friend class BasicCameraManager<CameraT>;

    struct CameraEdit {
        Entity entity{};
        CameraT camera{};
    };

    std::vector<CameraEdit> creates_{};
    std::vector<Entity> destroys_{};
    std::vector<CameraEdit> updates_{};
};

using CameraEditQueue2D = BasicCameraEditQueue<Camera2D>;
using CameraEditQueue3D = BasicCameraEditQueue<Camera3D>;

template <typename CameraT, typename InstanceT> class BasicCameraReadViewBase {
  public:
    struct Component {
        Entity entity{};
        CameraT camera{};
    };

    struct Snapshot {
        std::vector<Component> components{};
        std::vector<InstanceT> active_instances{};
        std::unordered_map<Entity, std::size_t, EntityHash> entity_to_component{};
        std::unordered_map<detail::StableSlotId, std::size_t, detail::StableSlotIdHash>
            slot_to_component{};
    };

    BasicCameraReadViewBase() = default;

    explicit BasicCameraReadViewBase(std::shared_ptr<const Snapshot> snapshot)
        : snapshot_(std::move(snapshot)) {
        if (snapshot_ == nullptr) {
            snapshot_ = std::make_shared<Snapshot>();
        }
    }

    [[nodiscard]] InstanceT instance(Entity entity) const {
        const auto position = snapshot_->entity_to_component.find(entity);
        if (position == snapshot_->entity_to_component.end()) {
            throw std::runtime_error("entity does not have a camera component");
        }
        return snapshot_->active_instances[position->second];
    }

    [[nodiscard]] const CameraT& camera(InstanceT instance) const {
        return component(instance).camera;
    }

    [[nodiscard]] const std::vector<InstanceT>& active_instances() const noexcept {
        return snapshot_->active_instances;
    }

  protected:
    [[nodiscard]] const Component& component(InstanceT instance) const {
        const auto position = snapshot_->slot_to_component.find(instance.slot);
        if (position == snapshot_->slot_to_component.end()) {
            throw std::runtime_error("camera instance is not part of this read view");
        }
        return snapshot_->components[position->second];
    }

    std::shared_ptr<const Snapshot> snapshot_ = std::make_shared<Snapshot>();
};

class CameraReadView2D : public BasicCameraReadViewBase<Camera2D, CameraInstance2D> {
  public:
    using BasicCameraReadViewBase::BasicCameraReadViewBase;

    [[nodiscard]] Camera2DView view(CameraInstance2D instance, float width, float height) const {
        return camera(instance).view(width, height);
    }
};

class CameraReadView3D : public BasicCameraReadViewBase<Camera3D, CameraInstance3D> {
  public:
    using BasicCameraReadViewBase::BasicCameraReadViewBase;

    [[nodiscard]] math::Mat4 projection_matrix(CameraInstance3D instance, float aspect) const {
        return camera(instance).projection_matrix(aspect);
    }

    [[nodiscard]] math::Mat4 view_matrix(CameraInstance3D instance,
                                         const TransformReadView3D& transforms) const {
        const Component& current = component(instance);
        return camera(instance).view_matrix(
            transforms.world_affine_matrix(transforms.instance(current.entity)));
    }

    [[nodiscard]] math::Mat4 view_projection_matrix(CameraInstance3D instance,
                                                    const TransformReadView3D& transforms,
                                                    float aspect) const {
        const Component& current = component(instance);
        return camera(instance).view_projection_matrix(
            transforms.world_affine_matrix(transforms.instance(current.entity)), aspect);
    }
};

template <typename CameraT> struct CameraTraits;

template <> struct CameraTraits<Camera2D> {
    using Instance = CameraInstance2D;
    using ReadView = CameraReadView2D;
};

template <> struct CameraTraits<Camera3D> {
    using Instance = CameraInstance3D;
    using ReadView = CameraReadView3D;
};

template <typename CameraT> class BasicCameraManager {
  public:
    using Instance = typename CameraTraits<CameraT>::Instance;
    using ReadView = typename CameraTraits<CameraT>::ReadView;
    using SnapshotComponent = typename ReadView::Component;
    using Snapshot = typename ReadView::Snapshot;

    [[nodiscard]] bool has_component(Entity entity) const {
        return entity_to_slot_.contains(entity);
    }

    [[nodiscard]] Instance instance(Entity entity) const {
        return Instance{.slot = slot_for(entity)};
    }

    [[nodiscard]] std::shared_ptr<const Snapshot> snapshot() const {
        return snapshot_;
    }

    void validate(const BasicCameraEditQueue<CameraT>& edits, const EntityManager& entities) const {
        std::unordered_set<Entity, EntityHash> existing{};
        for (const detail::StableSlotId slot_id : slots_.active_instances()) {
            existing.insert(slots_.get(slot_id).entity);
        }

        for (const auto& create : edits.creates_) {
            if (!create.entity || !entities.is_current(create.entity)) {
                throw std::runtime_error("camera create requires a current entity");
            }
            if (existing.contains(create.entity)) {
                throw std::runtime_error("entity already has a camera component");
            }
            existing.insert(create.entity);
        }

        for (const auto& update : edits.updates_) {
            if (!existing.contains(update.entity)) {
                throw std::runtime_error("camera update requires an existing component");
            }
        }

        for (const Entity entity : edits.destroys_) {
            if (!existing.contains(entity)) {
                throw std::runtime_error("camera destroy requires an existing component");
            }
            existing.erase(entity);
        }
    }

    void apply(const BasicCameraEditQueue<CameraT>& edits, std::uint64_t retire_epoch) {
        for (const auto& create : edits.creates_) {
            const detail::StableSlotId slot_id = slots_.create(Component{
                .entity = create.entity,
                .camera = create.camera,
            });
            entity_to_slot_[create.entity] = slot_id;
        }

        for (const auto& update : edits.updates_) {
            component_for(update.entity).camera = update.camera;
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
                .camera = component.camera,
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
        CameraT camera{};
    };

    [[nodiscard]] detail::StableSlotId slot_for(Entity entity) const {
        const auto position = entity_to_slot_.find(entity);
        if (position == entity_to_slot_.end()) {
            throw std::runtime_error("entity does not have a camera component");
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

using CameraManager2D = BasicCameraManager<Camera2D>;
using CameraManager3D = BasicCameraManager<Camera3D>;

} // namespace cubey
