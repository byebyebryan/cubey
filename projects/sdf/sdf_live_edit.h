#pragma once
#include "sdf_authored.h"
#include <array>
#include <optional>

namespace cubey::projects::sdf {
// Private frozen-base study metadata. The canonical edited program begins with
// an identical base prefix; no new scene/history/material format is introduced.
struct alignas(16) LiveEditControls {
    std::array<std::uint32_t, 4> header{};
    math::Vec4 info{};
};
static_assert(sizeof(LiveEditControls) == 32);
struct LiveEditProgram {
    AuthoredScene base, edited;
    LiveEditControls controls;
    std::size_t pending_leaves = 0;
};
[[nodiscard]] LiveEditProgram prepare_live_edit(AuthoredScene base, AuthoredScene edited,
                                                bool source_defined);
[[nodiscard]] float live_edit_cpu_distance(math::Vec3 point, const LiveEditProgram& program,
                                           bool source_defined,
                                           std::optional<float> stored_base = std::nullopt);
[[nodiscard]] float live_edit_cpu_base(math::Vec3 point, const AuthoredScene& base);
} // namespace cubey::projects::sdf
