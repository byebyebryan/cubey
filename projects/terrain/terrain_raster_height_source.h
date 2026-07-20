#pragma once

#include "terrain_height_source.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cubey::projects::terrain {

struct TerrainRasterProvenance {
    std::filesystem::path manifest_path{};
    std::string generator{};
    std::string code_revision{};
    std::string model_id{};
    std::string model_revision{};
    std::string purpose{};
    std::string selection{};
    std::string elevation_sha256{};
};

class TerrainRasterHeightSource final : public TerrainHeightSource {
  public:
    explicit TerrainRasterHeightSource(const std::filesystem::path& field_path);

    [[nodiscard]] TerrainHeightSourceMetadata metadata() const noexcept override;
    [[nodiscard]] float sample_height(const TerrainQuery& query) const override;

    [[nodiscard]] TerrainHeightSourceBounds bounds() const noexcept;
    [[nodiscard]] bool contains_disk(cubey::math::Vec2 center_xz, float radius_m) const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] float sample_spacing_m() const noexcept;
    [[nodiscard]] const TerrainRasterProvenance& provenance() const noexcept;

  private:
    struct Level {
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        float spacing_m = 0.0F;
        float origin_x_m = 0.0F;
        float origin_z_m = 0.0F;
        std::vector<float> values{};
    };

    [[nodiscard]] float sample_level(const Level& level, cubey::math::Vec2 world_xz) const;

    std::string source_id_{};
    std::uint64_t seed_ = 0U;
    float height_offset_m_ = 0.0F;
    float height_scale_ = 1.0F;
    float relief_scale_m_ = 1.0F;
    TerrainRasterProvenance provenance_{};
    std::vector<Level> levels_{};
};

} // namespace cubey::projects::terrain
