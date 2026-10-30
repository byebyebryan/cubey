#pragma once
#include "sdf_authored.h"
#include <array>

namespace cubey::projects::sdf {
// Private one-operation preview. Retained authored source/history is separate
// from this disposable rendering control and is used for canonical committing.
struct alignas(16) CachedEditControls {
    std::array<std::uint32_t, 4> header{}; // active, subtract, base instructions, reserved
    SceneInstruction primitive{};
};
static_assert(sizeof(CachedEditControls) == 80);
struct CachedEditProgram {
    AuthoredScene base, edited;
    CachedEditControls controls;
};
[[nodiscard]] CachedEditProgram prepare_cached_edit(AuthoredScene base, AuthoredScene edited);
} // namespace cubey::projects::sdf
