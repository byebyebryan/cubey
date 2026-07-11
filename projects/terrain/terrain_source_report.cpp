#include "terrain_source.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using cubey::projects::terrain::TerrainPreset;

constexpr std::array<TerrainPreset, 3> kPresets{
    TerrainPreset::Mountain,
    TerrainPreset::Upland,
    TerrainPreset::Plains,
};
constexpr std::array<std::uint64_t, 3> kSeeds{0U, 9012U, 12345U};
constexpr float kExtentM = 32'768.0F;
constexpr std::uint32_t kSamplesPerAxis = 65U;

} // namespace

int main() {
    nlohmann::json summaries = nlohmann::json::array();
    for (const TerrainPreset preset : kPresets) {
        for (const std::uint64_t seed : kSeeds) {
            const auto parameters = cubey::projects::terrain::resolve_terrain_source_parameters({
                .seed = seed,
                .preset = preset,
            });
            const auto summary = cubey::projects::terrain::summarize_terrain_source(
                parameters, {0.0F, 0.0F}, kExtentM, kSamplesPerAxis);
            summaries.push_back({
                {"preset", cubey::projects::terrain::terrain_preset_name(preset)},
                {"seed", seed},
                {"min_height_m", summary.min_height_m},
                {"max_height_m", summary.max_height_m},
                {"mean_height_m", summary.mean_height_m},
                {"relief_m", summary.max_height_m - summary.min_height_m},
                {"mean_slope", summary.mean_slope},
            });
        }
    }

    const nlohmann::json report{
        {"schema", "cubey.terrain.v1.source-summary"}, {"domain_extent_m", kExtentM},
        {"samples_per_axis", kSamplesPerAxis},         {"weathering", "off"},
        {"summaries", std::move(summaries)},
    };
    std::cout << report.dump(2) << '\n';
    return 0;
}
