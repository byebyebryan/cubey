#pragma once

#include "terrain_config.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainPhaseProfileMetadata {
    std::string app_name{};
    std::string recipe_id{};
    std::string camera_preset{};
    std::string preview_surface{};
    std::string preview_color{};
    std::uint32_t grid_width = 0;
    std::uint32_t grid_height = 0;
    float cell_size_m = 0.0F;
    std::uint32_t generator_revision = 0;
    std::uint32_t field_count = 0;
    std::uint32_t output_count = 0;
    std::uint64_t vertex_count = 0;
    std::uint64_t index_count = 0;
    std::uint64_t triangle_count = 0;
};

struct TerrainPhaseRecord {
    std::string name{};
    double duration_ms = 0.0;
};

class TerrainPhaseProfile {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TerrainPhaseProfile() = default;
    explicit TerrainPhaseProfile(std::filesystem::path output_prefix);

    [[nodiscard]] bool enabled() const noexcept {
        return !output_prefix_.empty();
    }

    [[nodiscard]] static TimePoint now() {
        return Clock::now();
    }

    [[nodiscard]] const std::filesystem::path& output_prefix() const noexcept {
        return output_prefix_;
    }

    void set_metadata(TerrainPhaseProfileMetadata metadata);
    void record_elapsed(std::string_view name, TimePoint start);
    void write() const;

  private:
    std::filesystem::path output_prefix_{};
    TerrainPhaseProfileMetadata metadata_{};
    std::vector<TerrainPhaseRecord> phases_{};
};

class TerrainPhaseScope {
  public:
    TerrainPhaseScope(TerrainPhaseProfile& profile, std::string_view name);
    ~TerrainPhaseScope();

    TerrainPhaseScope(const TerrainPhaseScope&) = delete;
    TerrainPhaseScope& operator=(const TerrainPhaseScope&) = delete;
    TerrainPhaseScope(TerrainPhaseScope&& other) noexcept;
    TerrainPhaseScope& operator=(TerrainPhaseScope&& other) noexcept;

  private:
    void finish() noexcept;

    TerrainPhaseProfile* profile_ = nullptr;
    std::string name_{};
    TerrainPhaseProfile::TimePoint start_{};
};

[[nodiscard]] std::filesystem::path terrain_phase_profile_output_prefix(std::string_view value);
[[nodiscard]] std::filesystem::path terrain_phase_profile_output_path(
    const std::filesystem::path& output_prefix);
[[nodiscard]] TerrainPhaseProfileMetadata terrain_phase_profile_metadata(
    std::string_view app_name, const TerrainRegionConfig& config);

} // namespace cubey::projects::terrain
