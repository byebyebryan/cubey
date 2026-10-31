#pragma once
#include "sdf_authored.h"
#include <array>

namespace cubey::projects::sdf {
struct alignas(16) CachedStrokeControls {
    std::array<std::uint32_t, 4> header{}; // count, subtract, base instructions, rounded admission
    std::array<SceneInstruction, 8> tools{};
};
static_assert(sizeof(CachedStrokeControls) == 528);
struct CachedStrokeProgram {
    AuthoredScene base, edited;
    CachedStrokeControls controls;
};
// Finite opaque same-operation root append; canonical source/history is retained.
CachedStrokeProgram prepare_cached_stroke(AuthoredScene base, AuthoredScene edited,
                                          bool rounded_segments = false);
} // namespace cubey::projects::sdf
