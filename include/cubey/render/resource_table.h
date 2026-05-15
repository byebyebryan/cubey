#pragma once

#include <cubey/render/resource_handle.h>

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cubey::render {

template <typename HandleT, typename ResourceT, typename HashT> class ResourceTable {
  public:
    [[nodiscard]] bool contains(HandleT handle) const {
        return resources_.contains(handle);
    }

    [[nodiscard]] bool empty() const noexcept {
        return resources_.empty();
    }

    template <typename... Args> ResourceT& emplace(HandleT handle, Args&&... args) {
        if (!handle) {
            throw std::runtime_error("resource table insert requires a non-null handle");
        }
        auto [position, inserted] = resources_.try_emplace(handle, std::forward<Args>(args)...);
        if (!inserted) {
            throw std::runtime_error("resource table already contains handle");
        }
        return position->second;
    }

    void erase(HandleT handle) {
        const std::size_t erased = resources_.erase(handle);
        if (erased == 0) {
            throw std::runtime_error("resource table erase requires an existing handle");
        }
    }

    void clear() {
        resources_.clear();
    }

    [[nodiscard]] ResourceT& at(HandleT handle) {
        const auto position = resources_.find(handle);
        if (position == resources_.end()) {
            throw std::runtime_error("resource table does not contain handle");
        }
        return position->second;
    }

    [[nodiscard]] const ResourceT& at(HandleT handle) const {
        const auto position = resources_.find(handle);
        if (position == resources_.end()) {
            throw std::runtime_error("resource table does not contain handle");
        }
        return position->second;
    }

    [[nodiscard]] const ResourceT& first() const {
        if (resources_.empty()) {
            throw std::runtime_error("resource table is empty");
        }
        return resources_.begin()->second;
    }

  private:
    std::unordered_map<HandleT, ResourceT, HashT> resources_{};
};

template <typename ResourceT>
using MeshResourceTable = ResourceTable<MeshHandle, ResourceT, MeshHandleHash>;

template <typename ResourceT>
using MaterialResourceTable = ResourceTable<MaterialHandle, ResourceT, MaterialHandleHash>;

} // namespace cubey::render
