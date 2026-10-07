#pragma once

#include "fluid_25d_recording.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

// Experimental presentation sidecar. Never accepted as hydraulic input.
// Sparse baked times fail closed; this is not a live producer or interpolator.
class Fluid25DDisplayCoverage {
  public:
    Fluid25DDisplayCoverage(const std::filesystem::path& path, const Fluid25DRecording& recording);
    [[nodiscard]] std::uint32_t subdivision() const noexcept {
        return subdivision_;
    }
    [[nodiscard]] const std::string& label() const noexcept {
        return label_;
    }
    [[nodiscard]] std::vector<float> frame(double saved_time_s) const;
    [[nodiscard]] bool has_frame(double saved_time_s) const noexcept;
    [[nodiscard]] std::vector<double> times_s() const;
    [[nodiscard]] std::size_t frame_bytes() const noexcept {
        return samples_ * sizeof(float);
    }

  private:
    struct Entry {
        double time_s;
        std::filesystem::path path;
        std::string sha256;
    };
    std::uint32_t subdivision_ = 0U;
    std::size_t samples_ = 0U;
    std::string label_;
    std::vector<Entry> entries_;
};

// Explicit opt-in window cache. Every payload is fully validated once before
// activation; steady playback returns immutable spans without file I/O/copies.
class Fluid25DDisplayCoverageCache {
  public:
    static constexpr std::size_t kByteBudget = 256U * 1024U * 1024U;
    explicit Fluid25DDisplayCoverageCache(const Fluid25DDisplayCoverage& source,
                                          std::span<const double> times,
                                          const std::function<void(std::size_t)>& progress = {});
    [[nodiscard]] std::span<const float> frame(double saved_time_s) const;
    [[nodiscard]] std::size_t bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] std::size_t frame_count() const noexcept {
        return frames_.size();
    }
    [[nodiscard]] double preparation_ms() const noexcept {
        return preparation_ms_;
    }

  private:
    std::map<double, std::vector<float>> frames_;
    std::size_t bytes_ = 0U;
    double preparation_ms_ = 0.0;
};

} // namespace cubey::projects::fluid::fluid_25d
