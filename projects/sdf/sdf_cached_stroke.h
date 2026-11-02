#pragma once
#include "sdf_authored.h"
#include <array>
#include <memory>

namespace cubey::projects::sdf {
struct alignas(16) CachedStrokeControls {
    std::array<std::uint32_t, 4> header{}; // count, subtract, base instructions, rounded admission
    std::array<SceneInstruction, 8> tools{};
};
static_assert(sizeof(CachedStrokeControls) == 528);
struct CachedStrokeProgram {
    AuthoredScene base, edited;
    CachedStrokeControls controls;
    double palette_ms = 0, validation_ms = 0;
    AuthoredSerializationStats serialization;
    double palette_reparse_ms = 0;
};
enum class StrokePreparation {
    Legacy,
    Palette
};
// Finite opaque same-operation root append; canonical source/history is retained.
CachedStrokeProgram
prepare_cached_stroke(AuthoredScene base, AuthoredScene edited, bool rounded_segments = false,
                      StrokePreparation preparation = StrokePreparation::Legacy);

struct StrokeVersion {
    std::uint64_t session = 0, base = 0, palette = 0;
    std::vector<std::uint64_t> pending_ids;
    bool operator==(const StrokeVersion&) const = default;
};
class PreparedCachedStroke {
  public:
    [[nodiscard]] const CachedStrokeProgram& program() const {
        return *program_;
    }
    void require_version(const StrokeVersion& current) const;

  private:
    PreparedCachedStroke(CachedStrokeProgram program, StrokeVersion version);
    std::shared_ptr<const CachedStrokeProgram> program_;
    StrokeVersion version_;
    friend PreparedCachedStroke seal_cached_stroke(AuthoredScene, AuthoredScene, StrokeVersion,
                                                   bool, StrokePreparation);
};
// Only this validating factory can create the owned immutable result. Caller
// supplies the live editor epoch and accepted prefix; builder checks it again.
PreparedCachedStroke seal_cached_stroke(AuthoredScene base, AuthoredScene edited,
                                        StrokeVersion version, bool rounded_segments = false,
                                        StrokePreparation preparation = StrokePreparation::Palette);
} // namespace cubey::projects::sdf
