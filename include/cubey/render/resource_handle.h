#pragma once

#include <cstddef>
#include <cstdint>

namespace cubey::render {

struct MeshHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(MeshHandle lhs, MeshHandle rhs) = default;
};

struct MeshHandleHash {
    [[nodiscard]] std::size_t operator()(MeshHandle handle) const noexcept {
        return (static_cast<std::size_t>(handle.index) * 16'777'619U) ^
               static_cast<std::size_t>(handle.generation);
    }
};

struct MaterialHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(MaterialHandle lhs, MaterialHandle rhs) = default;
};

struct MaterialHandleHash {
    [[nodiscard]] std::size_t operator()(MaterialHandle handle) const noexcept {
        return (static_cast<std::size_t>(handle.index) * 16'777'619U) ^
               static_cast<std::size_t>(handle.generation);
    }
};

} // namespace cubey::render
