#pragma once
#include "sdf_authored.h"
#include "sdf_timeline.h"
#include <span>

namespace cubey::projects::sdf {
void read_authored_history(AuthoredScene& scene);
[[nodiscard]] AuthoredScene authored_history_scene(const AuthoredScene& scene, std::size_t count);
struct AuthoredHistorySample {
    TimelineSample timeline;
    std::span<const SceneInstruction> cutters;
};
[[nodiscard]] AuthoredHistorySample sample_authored_history(const TimelineConfig& config,
                                                            const AuthoredScene& scene,
                                                            double seconds,
                                                            std::uint32_t debug = 0);
[[nodiscard]] double authored_history_seek(const TimelineConfig& config, const AuthoredScene& scene,
                                           std::size_t count);
} // namespace cubey::projects::sdf
