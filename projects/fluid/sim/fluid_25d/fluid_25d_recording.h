#pragma once

#include "fluid_25d_solver_state.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DRecordedFrameStats {
    double water_volume_m3 = 0.0;
    float max_depth_m = 0.0F;
    float max_speed_m_per_s = 0.0F;
    std::uint64_t dry_nonzero_momentum_cells = 0U;
};

struct Fluid25DRecordedFrame {
    double time_s = 0.0;
    double published_unix_s = 0.0; // Live ready time; zero for completed recordings.
    std::vector<float> depth_m{};
    std::vector<Fluid25DMomentum> momentum{};
    std::vector<Fluid25DVelocity> velocity{};
    Fluid25DRecordedFrameStats stats{};
};

struct Fluid25DRecordedRainKnot {
    double time_s = 0.0;
    double rate_m_per_s = 0.0;
};

enum class Fluid25DRecordedRainPhase : std::uint8_t {
    On = 0U,
    Tapering = 1U,
    Off = 2U,
};

struct Fluid25DRecordedRainStatus {
    double time_s = 0.0;
    double rate_mm_per_hour = 0.0;
    double cumulative_scheduled_rain_depth_mm = 0.0;
    Fluid25DRecordedRainPhase phase = Fluid25DRecordedRainPhase::Off;
};

[[nodiscard]] const char*
fluid_25d_recorded_rain_phase_name(Fluid25DRecordedRainPhase phase) noexcept;

// Strict, lazy reader for cubey.fluid25d.recording.v1 directories. Frame
// payloads are SHA-256 checked on load and at most three frames are retained
// by this reader's cache. Returned shared pointers remain valid if evicted.
class Fluid25DRecording {
  public:
    explicit Fluid25DRecording(const std::filesystem::path& recording_path,
                               bool live_stream = false);
    ~Fluid25DRecording();

    Fluid25DRecording(const Fluid25DRecording&) = delete;
    Fluid25DRecording& operator=(const Fluid25DRecording&) = delete;
    Fluid25DRecording(Fluid25DRecording&&) noexcept;
    Fluid25DRecording& operator=(Fluid25DRecording&&) noexcept;

    [[nodiscard]] std::uint32_t grid_width() const noexcept;
    [[nodiscard]] std::uint32_t grid_height() const noexcept;
    [[nodiscard]] float cell_size_m() const noexcept;
    [[nodiscard]] std::span<const float> bed() const noexcept;
    [[nodiscard]] std::span<const float> source_bed() const noexcept;
    [[nodiscard]] std::span<const double> times_s() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] std::size_t frame_index_at(double time_s) const;
    [[nodiscard]] std::shared_ptr<const Fluid25DRecordedFrame> frame(std::size_t index) const;
    [[nodiscard]] std::size_t cached_frame_count() const noexcept;

    [[nodiscard]] double rain_rate_mm_per_hour() const noexcept;
    [[nodiscard]] std::span<const Fluid25DRecordedRainKnot> rainfall_history() const noexcept;
    // Finite query times are clamped to the recorded rainfall-history endpoints.
    [[nodiscard]] Fluid25DRecordedRainStatus rain_status_at(double time_s) const;
    [[nodiscard]] const nlohmann::json& protocol() const noexcept;
    [[nodiscard]] const nlohmann::json& provenance() const noexcept;
    [[nodiscard]] const std::filesystem::path& manifest_path() const noexcept;
    [[nodiscard]] bool is_live_stream() const noexcept;
    [[nodiscard]] const std::string& session_id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const std::string& producer_state() const noexcept;
    [[nodiscard]] const std::string& producer_message() const noexcept;
    [[nodiscard]] double latest_native_time_s() const noexcept;
    [[nodiscard]] bool native_running() const noexcept;
    // Checks identity, immutable metadata/prefix and monotone lifecycle. No I/O.
    void validate_successor_of(const Fluid25DRecording& previous) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A pure saved-state playback clock. It never interpolates physical fields;
// frame_index() selects the saved frame at or before the current playhead.
class Fluid25DRecordingPlayback {
  public:
    explicit Fluid25DRecordingPlayback(std::span<const double> saved_times_s);

    void advance(double wall_delta_s);
    void seek(double time_s);
    void restart() noexcept;
    void previous();
    void next();
    void set_paused(bool paused) noexcept;
    void set_rate(double rate);
    // Live only: extend the available prefix without restarting the playhead.
    void extend(std::span<const double> saved_times_s);

    [[nodiscard]] std::size_t frame_index() const noexcept;
    [[nodiscard]] double time_s() const noexcept;
    [[nodiscard]] double rate() const noexcept;
    [[nodiscard]] bool paused() const noexcept;
    [[nodiscard]] bool ended() const noexcept;
    [[nodiscard]] std::span<const double> saved_times_s() const noexcept;

  private:
    std::vector<double> times_s_{};
    double time_s_ = 0.0;
    double rate_ = 1.0;
    bool paused_ = true;
    bool ended_ = false;
};

} // namespace cubey::projects::fluid::fluid_25d
