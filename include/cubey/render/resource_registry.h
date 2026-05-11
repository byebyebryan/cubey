#pragma once

#include <cubey/render/resource_handle.h>

#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::render {

namespace detail {

template <typename HandleT> class ResourceHandleStore {
  public:
    ResourceHandleStore() {
        slots_.resize(1);
    }

    [[nodiscard]] HandleT create(std::string label = {}) {
        std::lock_guard const lock(mutex_);

        std::uint32_t index = 0;
        if (!free_indices_.empty()) {
            index = free_indices_.front();
            free_indices_.pop_front();
        } else {
            const auto max_index =
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
            if (slots_.size() >= max_index) {
                throw std::runtime_error("render resource handle store is full");
            }
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        }

        Slot& slot = slots_[index];
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        slot.alive = true;
        slot.label = std::move(label);
        return HandleT{.index = index, .generation = slot.generation};
    }

    void destroy(HandleT handle) {
        std::lock_guard const lock(mutex_);
        Slot& slot = validated_slot(handle);
        advance_generation(slot);
        slot.alive = false;
        slot.label.clear();
        free_indices_.push_back(handle.index);
    }

    [[nodiscard]] bool is_alive(HandleT handle) const {
        std::lock_guard const lock(mutex_);
        return is_alive_locked(handle);
    }

    [[nodiscard]] std::string label(HandleT handle) const {
        std::lock_guard const lock(mutex_);
        return validated_slot(handle).label;
    }

  private:
    struct Slot {
        std::uint32_t generation = 0;
        bool alive = false;
        std::string label{};
    };

    static void advance_generation(Slot& slot) {
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
    }

    [[nodiscard]] bool is_alive_locked(HandleT handle) const {
        if (!handle || handle.index >= slots_.size()) {
            return false;
        }
        const Slot& slot = slots_[handle.index];
        return slot.alive && slot.generation == handle.generation;
    }

    [[nodiscard]] Slot& validated_slot(HandleT handle) {
        if (!is_alive_locked(handle)) {
            throw std::runtime_error("render resource handle is invalid");
        }
        return slots_[handle.index];
    }

    [[nodiscard]] const Slot& validated_slot(HandleT handle) const {
        if (!is_alive_locked(handle)) {
            throw std::runtime_error("render resource handle is invalid");
        }
        return slots_[handle.index];
    }

    mutable std::mutex mutex_{};
    std::vector<Slot> slots_{};
    std::deque<std::uint32_t> free_indices_{};
};

} // namespace detail

class RenderResourceRegistry {
  public:
    [[nodiscard]] MeshHandle create_mesh(std::string label = {}) {
        return meshes_.create(std::move(label));
    }

    [[nodiscard]] MaterialHandle create_material(std::string label = {}) {
        return materials_.create(std::move(label));
    }

    void destroy_mesh(MeshHandle handle) {
        meshes_.destroy(handle);
    }

    void destroy_material(MaterialHandle handle) {
        materials_.destroy(handle);
    }

    [[nodiscard]] bool is_alive(MeshHandle handle) const {
        return meshes_.is_alive(handle);
    }

    [[nodiscard]] bool is_alive(MaterialHandle handle) const {
        return materials_.is_alive(handle);
    }

    [[nodiscard]] std::string label(MeshHandle handle) const {
        return meshes_.label(handle);
    }

    [[nodiscard]] std::string label(MaterialHandle handle) const {
        return materials_.label(handle);
    }

  private:
    detail::ResourceHandleStore<MeshHandle> meshes_{};
    detail::ResourceHandleStore<MaterialHandle> materials_{};
};

} // namespace cubey::render
