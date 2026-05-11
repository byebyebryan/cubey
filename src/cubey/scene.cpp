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
      transforms2d_(std::move(other.transforms2d_)), transforms3d_(std::move(other.transforms3d_)),
      cameras2d_(std::move(other.cameras2d_)), cameras3d_(std::move(other.cameras3d_)),
      renderables3d_(std::move(other.renderables3d_)), lights3d_(std::move(other.lights3d_)),
      committed_(std::exchange(other.committed_, true)) {}

SceneEditQueue& SceneEditQueue::operator=(SceneEditQueue&& other) noexcept {
    if (this != &other) {
        if (!committed_) {
            rollback_reserved_entities();
        }
        entities_ = std::exchange(other.entities_, nullptr);
        reserved_entities_ = std::move(other.reserved_entities_);
        destroyed_entities_ = std::move(other.destroyed_entities_);
        transforms2d_ = std::move(other.transforms2d_);
        transforms3d_ = std::move(other.transforms3d_);
        cameras2d_ = std::move(other.cameras2d_);
        cameras3d_ = std::move(other.cameras3d_);
        renderables3d_ = std::move(other.renderables3d_);
        lights3d_ = std::move(other.lights3d_);
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
    transforms2d_ = TransformEditQueue2D{};
    transforms3d_ = TransformEditQueue3D{};
    cameras2d_ = CameraEditQueue2D{};
    cameras3d_ = CameraEditQueue3D{};
    renderables3d_ = RenderableEditQueue3D{};
    lights3d_ = LightEditQueue3D{};
}

SceneTransactionEntities::SceneTransactionEntities(SceneEditQueue& edits) : edits_(&edits) {}

Entity SceneTransactionEntities::create() {
    return edits_->create_entity();
}

SceneTransaction::SceneTransaction(Scene& scene) : scene_(&scene), edits_(scene.entities()) {}

SceneTransactionEntities SceneTransaction::entities() {
    return SceneTransactionEntities(edits_);
}

TransformEditQueue2D& SceneTransaction::transforms2d() noexcept {
    return edits_.transforms2d();
}

TransformEditQueue3D& SceneTransaction::transforms3d() noexcept {
    return edits_.transforms3d();
}

CameraEditQueue2D& SceneTransaction::cameras2d() noexcept {
    return edits_.cameras2d();
}

CameraEditQueue3D& SceneTransaction::cameras3d() noexcept {
    return edits_.cameras3d();
}

RenderableEditQueue3D& SceneTransaction::renderables3d() noexcept {
    return edits_.renderables3d();
}

LightEditQueue3D& SceneTransaction::lights3d() noexcept {
    return edits_.lights3d();
}

void SceneTransaction::commit() {
    if (scene_ == nullptr) {
        throw std::runtime_error("scene transaction is detached");
    }
    scene_->commit(edits_);
    scene_ = nullptr;
}

SceneReadView::SceneReadView(Scene& scene, std::uint64_t epoch)
    : scene_(&scene), epoch_(epoch), transforms2d_(scene.transforms2d_.snapshot()),
      transforms3d_(scene.transforms3d_.snapshot()), cameras2d_(scene.cameras2d_.snapshot()),
      cameras3d_(scene.cameras3d_.snapshot()), renderables3d_(scene.renderables3d_.snapshot()),
      lights3d_(scene.lights3d_.snapshot()) {}

SceneReadView::~SceneReadView() {
    release();
}

SceneReadView::SceneReadView(SceneReadView&& other) noexcept
    : scene_(std::exchange(other.scene_, nullptr)), epoch_(std::exchange(other.epoch_, 0)),
      transforms2d_(std::move(other.transforms2d_)), transforms3d_(std::move(other.transforms3d_)),
      cameras2d_(std::move(other.cameras2d_)), cameras3d_(std::move(other.cameras3d_)),
      renderables3d_(std::move(other.renderables3d_)), lights3d_(std::move(other.lights3d_)) {}

SceneReadView& SceneReadView::operator=(SceneReadView&& other) noexcept {
    if (this != &other) {
        release();
        scene_ = std::exchange(other.scene_, nullptr);
        epoch_ = std::exchange(other.epoch_, 0);
        transforms2d_ = std::move(other.transforms2d_);
        transforms3d_ = std::move(other.transforms3d_);
        cameras2d_ = std::move(other.cameras2d_);
        cameras3d_ = std::move(other.cameras3d_);
        renderables3d_ = std::move(other.renderables3d_);
        lights3d_ = std::move(other.lights3d_);
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

Scene::Scene(const render::RenderResourceRegistry* render_resources)
    : render_resources_(render_resources) {}

SceneEditQueue Scene::create_edit_queue() {
    return SceneEditQueue(entities_);
}

SceneTransaction Scene::begin_transaction() {
    return SceneTransaction(*this);
}

SceneReadView Scene::read() {
    std::lock_guard const edit_lock(edit_mutex_);
    std::lock_guard const lock(read_mutex_);
    active_read_epochs_.push_back(current_epoch_);
    return SceneReadView(*this, current_epoch_);
}

void Scene::commit(SceneEditQueue& edits) {
    std::lock_guard const edit_lock(edit_mutex_);
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
            transforms2d_.validate_destroy_entity(entity);
            transforms3d_.validate_destroy_entity(entity);
        }
        transforms2d_.validate(edits.transforms2d_, entities_);
        transforms3d_.validate(edits.transforms3d_, entities_);
        cameras2d_.validate(edits.cameras2d_, entities_);
        cameras3d_.validate(edits.cameras3d_, entities_);
        renderables3d_.validate(edits.renderables3d_, entities_, render_resources_);
        lights3d_.validate(edits.lights3d_, entities_);

        ++current_epoch_;

        for (const Entity entity : edits.reserved_entities_) {
            entities_.publish(entity);
        }
        transforms2d_.apply(edits.transforms2d_, current_epoch_);
        transforms3d_.apply(edits.transforms3d_, current_epoch_);
        cameras2d_.apply(edits.cameras2d_, current_epoch_);
        cameras3d_.apply(edits.cameras3d_, current_epoch_);
        renderables3d_.apply(edits.renderables3d_, current_epoch_);
        lights3d_.apply(edits.lights3d_, current_epoch_);
        for (const Entity entity : edits.destroyed_entities_) {
            transforms2d_.destroy_entity_if_exists(entity, current_epoch_);
            transforms3d_.destroy_entity_if_exists(entity, current_epoch_);
            cameras2d_.destroy_entity_if_exists(entity, current_epoch_);
            cameras3d_.destroy_entity_if_exists(entity, current_epoch_);
            renderables3d_.destroy_entity_if_exists(entity, current_epoch_);
            lights3d_.destroy_entity_if_exists(entity, current_epoch_);
            entities_.destroy(entity, current_epoch_);
        }
        transforms2d_.update_world_matrices();
        transforms3d_.update_world_matrices();
        transforms2d_.publish_snapshot();
        transforms3d_.publish_snapshot();
        cameras2d_.publish_snapshot();
        cameras3d_.publish_snapshot();
        renderables3d_.publish_snapshot();
        lights3d_.publish_snapshot();
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
    transforms2d_.retire_destroyed_up_to(safe_epoch);
    transforms3d_.retire_destroyed_up_to(safe_epoch);
    cameras2d_.retire_destroyed_up_to(safe_epoch);
    cameras3d_.retire_destroyed_up_to(safe_epoch);
    renderables3d_.retire_destroyed_up_to(safe_epoch);
    lights3d_.retire_destroyed_up_to(safe_epoch);
}

} // namespace cubey
