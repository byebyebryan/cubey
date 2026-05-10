#pragma once

#include <cubey/entity.h>
#include <cubey/transform_manager.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace cubey {

class Scene;

class SceneEditQueue {
  public:
    explicit SceneEditQueue(EntityManager& entities);
    ~SceneEditQueue();

    SceneEditQueue(const SceneEditQueue&) = delete;
    SceneEditQueue& operator=(const SceneEditQueue&) = delete;
    SceneEditQueue(SceneEditQueue&& other) noexcept;
    SceneEditQueue& operator=(SceneEditQueue&& other) noexcept;

    [[nodiscard]] Entity create_entity();
    void destroy(Entity entity);

    [[nodiscard]] TransformEditQueue2D& transforms2d() noexcept {
        return transforms2d_;
    }

    [[nodiscard]] TransformEditQueue3D& transforms3d() noexcept {
        return transforms3d_;
    }

  private:
    friend class Scene;

    void rollback_reserved_entities() noexcept;
    void mark_committed() noexcept;

    EntityManager* entities_ = nullptr;
    std::vector<Entity> reserved_entities_{};
    std::vector<Entity> destroyed_entities_{};
    TransformEditQueue2D transforms2d_{};
    TransformEditQueue3D transforms3d_{};
    bool committed_ = false;
};

class SceneTransactionEntities {
  public:
    explicit SceneTransactionEntities(SceneEditQueue& edits);

    [[nodiscard]] Entity create();

  private:
    SceneEditQueue* edits_ = nullptr;
};

class SceneTransaction {
  public:
    explicit SceneTransaction(Scene& scene);

    SceneTransaction(const SceneTransaction&) = delete;
    SceneTransaction& operator=(const SceneTransaction&) = delete;
    SceneTransaction(SceneTransaction&&) = delete;
    SceneTransaction& operator=(SceneTransaction&&) = delete;

    [[nodiscard]] SceneTransactionEntities entities();
    [[nodiscard]] TransformEditQueue2D& transforms2d() noexcept;
    [[nodiscard]] TransformEditQueue3D& transforms3d() noexcept;
    void commit();

  private:
    Scene* scene_ = nullptr;
    SceneEditQueue edits_;
};

class SceneReadView {
  public:
    SceneReadView() = default;
    SceneReadView(Scene& scene, std::uint64_t epoch);
    ~SceneReadView();

    SceneReadView(const SceneReadView&) = delete;
    SceneReadView& operator=(const SceneReadView&) = delete;
    SceneReadView(SceneReadView&& other) noexcept;
    SceneReadView& operator=(SceneReadView&& other) noexcept;

    [[nodiscard]] std::uint64_t epoch() const noexcept {
        return epoch_;
    }

    [[nodiscard]] const TransformReadView2D& transforms2d() const noexcept {
        return transforms2d_;
    }

    [[nodiscard]] const TransformReadView3D& transforms3d() const noexcept {
        return transforms3d_;
    }

  private:
    void release() noexcept;

    Scene* scene_ = nullptr;
    std::uint64_t epoch_ = 0;
    TransformReadView2D transforms2d_{};
    TransformReadView3D transforms3d_{};
};

class Scene {
  public:
    [[nodiscard]] EntityManager& entities() noexcept {
        return entities_;
    }

    [[nodiscard]] const EntityManager& entities() const noexcept {
        return entities_;
    }

    [[nodiscard]] std::uint64_t epoch() const noexcept {
        return current_epoch_;
    }

    [[nodiscard]] SceneEditQueue create_edit_queue();
    [[nodiscard]] SceneTransaction begin_transaction();
    [[nodiscard]] SceneReadView read();

    void commit(SceneEditQueue& edits);

  private:
    friend class SceneReadView;

    void release_read_view(std::uint64_t epoch) noexcept;
    void retire_safe_entities() noexcept;

    EntityManager entities_{};
    TransformManager2D transforms2d_{};
    TransformManager3D transforms3d_{};
    std::uint64_t current_epoch_ = 0;
    std::mutex edit_mutex_{};
    std::mutex read_mutex_{};
    std::vector<std::uint64_t> active_read_epochs_{};
};

} // namespace cubey
