#pragma once

#include <cubey/asset/terrain_raster_height_source.h>
#include <cubey/core/math.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cubey::asset {

// Absolute source XZ, cell centers, row-major with X fastest and Z positive.
struct TerrainWaterCaptureGrid {
    std::uint32_t width = 0U, height = 0U;
    float cell_size_m = 0.0F;
    math::Vec2 sample_origin_xz_m{0.0F};
    [[nodiscard]] math::Vec2 center_xz_m() const noexcept;
};

// Immutable, single rain-fed state. Velocity is XZ in m/s, not momentum.
// Capturing does not advance water; visual time belongs to the consumer.
struct TerrainWaterCapture {
    TerrainWaterCaptureGrid grid{};
    std::string source_elevation_sha256{};
    double capture_time_s = 0.0;
    double rain_mm_per_hour = 0.0;
    std::string recording_sha256{}, frame_sha256{};
    std::vector<float> bed_m{}, depth_m{};
    std::vector<math::Vec2> velocity_xz_m_per_s{};
};

void validate_terrain_water_capture(const TerrainWaterCapture& capture);
void validate_terrain_water_capture_source(const TerrainWaterCapture& capture,
                                           const TerrainRasterHeightSource& source);
[[nodiscard]] TerrainWaterCapture load_terrain_water_capture(const std::filesystem::path& path);
// Explicit offline export only. Refuses existing directories; publishes manifest last.
void write_terrain_water_capture(const std::filesystem::path& directory,
                                 const TerrainWaterCapture& capture);

} // namespace cubey::asset
