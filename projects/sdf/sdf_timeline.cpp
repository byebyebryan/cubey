#include "sdf_timeline.h"
#include "sdf_recipe.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace cubey::projects::sdf {
void validate_timeline(const TimelineConfig& c) {
    if (!std::isfinite(c.duration) || c.duration <= 0 || !std::isfinite(c.construction_seconds) ||
        c.construction_seconds <= 0 ||
        ((c.motion == "construction" || c.motion == "presentation") &&
         c.construction_seconds > c.duration) ||
        !std::isfinite(c.orbit_turns) ||
        (c.motion == "presentation" && c.construction_seconds >= c.duration * 5.0 / 6.0) ||
        (c.motion != "static" && c.motion != "orbit" && c.motion != "construction" &&
         c.motion != "presentation"))
        throw std::invalid_argument("invalid SDF motion/duration/construction timeline");
}
TimelineSample sample_timeline(const TimelineConfig& c, std::size_t count, double seconds,
                               std::uint32_t debug) {
    validate_timeline(c);
    if (!std::isfinite(seconds))
        throw std::invalid_argument("SDF logical time must be finite");
    const double t = std::clamp(seconds, 0.0, c.duration);
    TimelineSample result{count, 0, debug};
    const float tau = 2 * std::numbers::pi_v<float>;
    if (c.motion == "orbit")
        result.yaw = tau * c.orbit_turns * static_cast<float>(t / c.duration);
    if (c.motion == "construction" || c.motion == "presentation")
        result.prefix = construction_prefix(count, t, c.construction_seconds);
    if (c.motion == "presentation") {
        const double orbit_end = c.duration * 5.0 / 6.0;
        result.yaw = t <= c.construction_seconds
                         ? 0.35F * static_cast<float>(t / c.construction_seconds)
                         : 0.35F + tau * c.orbit_turns *
                                       static_cast<float>(
                                           std::clamp((t - c.construction_seconds) /
                                                          (orbit_end - c.construction_seconds),
                                                      0.0, 1.0));
        if (t >= orbit_end)
            result.debug = t < c.duration * 11.0 / 12.0 ? 3U : 4U;
    }
    return result;
}
} // namespace cubey::projects::sdf
