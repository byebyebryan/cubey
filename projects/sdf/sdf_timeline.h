#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace cubey::projects::sdf {
struct TimelineConfig {
    std::string motion = "static";
    double duration = 24, construction_seconds = 8;
    float orbit_turns = 1;
};
struct TimelineSample {
    std::size_t prefix = 0;
    float yaw = 0;
    std::uint32_t debug = 0;
};
void validate_timeline(const TimelineConfig& config);
[[nodiscard]] TimelineSample sample_timeline(const TimelineConfig& config, std::size_t count,
                                             double seconds, std::uint32_t debug = 0);
} // namespace cubey::projects::sdf
