#include <cubey/scene.h>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace cubey {

SceneEditQueue::SceneEditQueue(EntityManager& entities) : entities_(&entities) {}

SceneEditQueue::~SceneEditQueue() {
    if (!committed_) {
        rollback_reserved_entities();
    }
}

SceneEditQueue::SceneEditQueue(SceneEditQueue&& other) noexcept
    : entities_(std::exchange(other.entities_, nullptr)),
      reserved_entities_(std::move(other.reserved_entities_)),
      destroyed_entities_(std::move(other.destroyed_entities_)),
      committed_(std::exchange(other.committed_, true)) {}

SceneEditQueue& SceneEditQueue::operator=(SceneEditQueue&& other) noexcept {
    if (this != &other) {
        if (!committed_) {
            rollback_reserved_entities();
        }
        entities_ = std::exchange(other.entities_, nullptr);
        reserved_entities_ = std::move(other.reserved_entities_);
        destroyed_entities_ = std::move(other.destroyed_entities_);
        committed_ = std::exchange(other.committed_, true);
    }
    return *this;
}

Entity SceneEditQueue::create_entity() {
    if (entities_ == nullptr) {
        throw std::runtime_error("scene edit queue is detached");
    }
    Entity entity = entities_->reserve();
    reserved_entities_.push_back(entity);
    return entity;
}

void SceneEditQueue::destroy(Entity entity) {
    destroyed_entities_.push_back(entity);
}

void SceneEditQueue::rollback_reserved_entities() noexcept {
    if (entities_ == nullptr) {
        reserved_entities_.clear();
        return;
    }

    for (const Entity entity : reserved_entities_) {
        try {
            if (entities_->is_reserved(entity)) {
                entities_->rollback_reserved(entity);
            }
        } catch (const std::exception&) {
        }
    }
    reserved_entities_.clear();
}

void SceneEditQueue::mark_committed() noexcept {
    committed_ = true;
    entities_ = nullptr;
    reserved_entities_.clear();
    destroyed_entities_.clear();
}

SceneTransactionEntities::SceneTransactionEntities(SceneEditQueue& edits) : edits_(&edits) {}

Entity SceneTransactionEntities::create() {
    return edits_->create_entity();
}

SceneTransaction::SceneTransaction(Scene& scene) : scene_(&scene), edits_(scene.entities()) {}

SceneTransactionEntities SceneTransaction::entities() {
    return SceneTransactionEntities(edits_);
}

void SceneTransaction::commit() {
    if (scene_ == nullptr) {
        throw std::runtime_error("scene transaction is detached");
    }
    scene_->commit(edits_);
    scene_ = nullptr;
}

SceneReadView::SceneReadView(Scene& scene, std::uint64_t epoch) : scene_(&scene), epoch_(epoch) {}

SceneReadView::~SceneReadView() {
    release();
}

SceneReadView::SceneReadView(SceneReadView&& other) noexcept
    : scene_(std::exchange(other.scene_, nullptr)), epoch_(std::exchange(other.epoch_, 0)) {}

SceneReadView& SceneReadView::operator=(SceneReadView&& other) noexcept {
    if (this != &other) {
        release();
        scene_ = std::exchange(other.scene_, nullptr);
        epoch_ = std::exchange(other.epoch_, 0);
    }
    return *this;
}

void SceneReadView::release() noexcept {
    if (scene_ != nullptr) {
        scene_->release_read_view(epoch_);
        scene_ = nullptr;
        epoch_ = 0;
    }
}

SceneEditQueue Scene::create_edit_queue() {
    return SceneEditQueue(entities_);
}

SceneTransaction Scene::begin_transaction() {
    return SceneTransaction(*this);
}

SceneReadView Scene::read() {
    std::lock_guard const lock(read_mutex_);
    active_read_epochs_.push_back(current_epoch_);
    return SceneReadView(*this, current_epoch_);
}

void Scene::commit(SceneEditQueue& edits) {
    if (edits.entities_ != &entities_) {
        throw std::runtime_error("scene edit queue belongs to a different scene");
    }
    if (edits.committed_) {
        throw std::runtime_error("scene edit queue was already committed");
    }

    try {
        for (const Entity entity : edits.destroyed_entities_) {
            if (!entities_.is_alive(entity)) {
                throw std::runtime_error("scene destroy edit requires a live entity");
            }
        }

        ++current_epoch_;

        for (const Entity entity : edits.reserved_entities_) {
            entities_.publish(entity);
        }
        for (const Entity entity : edits.destroyed_entities_) {
            entities_.destroy(entity, current_epoch_);
        }
        edits.mark_committed();
        retire_safe_entities();
    } catch (const std::exception&) {
        edits.rollback_reserved_entities();
        throw;
    }
}

void Scene::release_read_view(std::uint64_t epoch) noexcept {
    {
        std::lock_guard const lock(read_mutex_);
        auto position = std::find(active_read_epochs_.begin(), active_read_epochs_.end(), epoch);
        if (position != active_read_epochs_.end()) {
            active_read_epochs_.erase(position);
        }
    }
    retire_safe_entities();
}

void Scene::retire_safe_entities() noexcept {
    std::uint64_t safe_epoch = current_epoch_;
    {
        std::lock_guard const lock(read_mutex_);
        if (!active_read_epochs_.empty()) {
            safe_epoch = *std::min_element(active_read_epochs_.begin(), active_read_epochs_.end());
        }
    }
    static_cast<void>(entities_.retire_destroyed_up_to(safe_epoch));
}

} // namespace cubey
