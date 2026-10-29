#pragma once
#include "sdf_authored.h"

namespace cubey::projects::sdf {
// Disposable derived data. Never serialized into the authored source/history.
struct PreparedCones {
    static constexpr unsigned version = 1;
    std::uint64_t geometry_hash = 0;
    std::size_t instruction_count = 0;
    std::vector<math::Vec4> entries; // slope, radial, height*radial, eligible
    [[nodiscard]] bool matches(const AuthoredScene& scene) const;
};
[[nodiscard]] PreparedCones prepare_cones(const AuthoredScene& scene);
} // namespace cubey::projects::sdf
