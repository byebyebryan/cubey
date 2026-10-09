#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace cubey::projects::fluid::fluid_25d {

// Presentation only: no drop is a water parcel and this never supplies mass.
// A stable prefix of a fixed world-space seed set avoids re-randomizing rain
// when the applied rate changes. Compress extreme demonstration rainfall.
inline constexpr std::uint32_t kFluid25DRainVisualMaxStreaks = 24000U;

struct Fluid25DRainVisualVolume {
    float floor_m = 0.0F;
    float height_m = 0.0F;
};

// One scene-wide weather volume, never one terrain-offset lid per column.
// Keep its roof well above the highest peak; local bed+h only clips impacts.
[[nodiscard]] inline Fluid25DRainVisualVolume
fluid_25d_rain_visual_volume(float terrain_low_m, float terrain_high_m, float domain_m,
                             float height_scale) noexcept {
    const float streak_m = std::clamp(domain_m * 0.007F, 2.0F, 100.0F);
    const float roof_padding_m = std::clamp(domain_m * 0.15F, 80.0F, 2400.0F);
    return {terrain_low_m * height_scale - streak_m,
            (terrain_high_m - terrain_low_m) * height_scale + roof_padding_m + streak_m};
}

[[nodiscard]] inline std::uint32_t
fluid_25d_rain_visual_count(bool enabled, std::optional<double> applied_mm_per_hour,
                            float strength) noexcept {
    if (!enabled || !applied_mm_per_hour || !std::isfinite(*applied_mm_per_hour) ||
        *applied_mm_per_hour <= 0.0 || !std::isfinite(strength) || strength <= 0.0F)
        return 0U;
    const double density = std::clamp(std::sqrt(*applied_mm_per_hour / 1024.0) *
                                          std::clamp(double(strength), 0.0, 2.0),
                                      0.0, 1.0);
    return static_cast<std::uint32_t>(density * kFluid25DRainVisualMaxStreaks);
}

// Independent of physical playback speed/pacing. Explicit seek/reset restarts
// it; pause, unavailable fields and unhealthy live sessions hold it. Capture
// uses index/fps, not physical requested intervals or machine wall time.
class Fluid25DRainVisualClock {
  public:
    void reset() noexcept {
        seconds_ = 0.0;
        motion_seconds_ = 0.0;
    }
    void advance(double wall_delta_s, bool running, float speed = 1.0F) noexcept {
        if (running && std::isfinite(wall_delta_s) && std::isfinite(speed)) {
            const double delta = std::clamp(wall_delta_s, 0.0, 0.25);
            seconds_ += delta;
            // Integrate speed, rather than multiplying accumulated time: GUI
            // speed edits change the slope without jumping/re-seeding drops.
            motion_seconds_ += delta * std::clamp(double(speed), 0.25, 8.0);
        }
    }
    void capture(std::uint32_t index, std::uint32_t fps, bool video, float speed = 1.0F) noexcept {
        seconds_ = video && fps != 0U ? double(index) / double(fps) : 0.0;
        motion_seconds_ =
            seconds_ * (std::isfinite(speed) ? std::clamp(double(speed), 0.25, 8.0) : 1.0);
    }
    [[nodiscard]] double seconds() const noexcept {
        return seconds_;
    }
    [[nodiscard]] double motion_seconds() const noexcept {
        return motion_seconds_;
    }

  private:
    double seconds_ = 0.0;
    double motion_seconds_ = 0.0;
};

struct Fluid25DRainVisualFrame {
    std::uint32_t streak_count = 0U;
    double clock_s = 0.0;
    double motion_s = 0.0;
};

} // namespace cubey::projects::fluid::fluid_25d
