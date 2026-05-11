#pragma once

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

} // namespace cubey::render
