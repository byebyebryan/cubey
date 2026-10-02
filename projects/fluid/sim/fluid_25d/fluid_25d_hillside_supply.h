#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {

// App-owned forcing policy. Queries use the next fixed step's start time;
// wall time, playback, rendering, and pause never advance this policy.
class Fluid25DHillsideSupply {
  public:
    explicit Fluid25DHillsideSupply(bool response = false) : response_(response) {}

    void queue_preset(float m3_per_s) {
        if (m3_per_s != 50.0F && m3_per_s != 100.0F && m3_per_s != 150.0F)
            throw std::runtime_error("hillside supply preset must be 50, 100 or 150 m3/s");
        pending_ = m3_per_s;
    }
    void apply_pending() noexcept {
        if (pending_) {
            manual_ = pending_;
            pending_.reset();
        }
    }
    [[nodiscard]] float rate(double step_start_seconds, float reference_rate) const {
        if (!std::isfinite(step_start_seconds) || step_start_seconds < 0.0 ||
            !std::isfinite(reference_rate) || reference_rate <= 0.0F)
            throw std::runtime_error("invalid hillside supply clock or reference rate");
        if (manual_)
            return *manual_;
        if (!response_)
            return reference_rate;
        if (step_start_seconds < 3600.0 || step_start_seconds >= 7200.0)
            return 100.0F;
        return step_start_seconds < 5400.0 ? 150.0F : 50.0F;
    }
    [[nodiscard]] std::optional<float> pending() const noexcept {
        return pending_;
    }
    [[nodiscard]] bool manual() const noexcept {
        return manual_.has_value();
    }
    [[nodiscard]] bool response() const noexcept {
        return response_;
    }
    void reset() noexcept {
        manual_.reset();
        pending_.reset();
    }

  private:
    bool response_ = false;
    std::optional<float> manual_{};
    std::optional<float> pending_{};
};

} // namespace cubey::projects::fluid::fluid_25d
