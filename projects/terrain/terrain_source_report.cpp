#include "terrain_source.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

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

int main(int argc, char** argv) {
    cubey::projects::terrain::TerrainSourceVersion version =
        cubey::projects::terrain::TerrainSourceVersion::V1;
    if (argc == 3 && std::string_view(argv[1]) == "--source-version") {
        version = cubey::projects::terrain::terrain_source_version_from_name(argv[2]);
    } else if (argc != 1) {
        throw std::runtime_error("usage: terrain_source_report [--source-version v1|v2|v2.1|v3]");
    }

    nlohmann::json summaries = nlohmann::json::array();
    for (const TerrainPreset preset : kPresets) {
        if (version != cubey::projects::terrain::TerrainSourceVersion::V1 &&
            preset != TerrainPreset::Mountain) {
            continue;
        }
        for (const std::uint64_t seed : kSeeds) {
            const auto parameters = cubey::projects::terrain::resolve_terrain_source_parameters({
                .seed = seed,
                .preset = preset,
                .version = version,
            });
            const auto summary = cubey::projects::terrain::summarize_terrain_source(
                parameters, {0.0F, 0.0F}, kExtentM, kSamplesPerAxis);
            nlohmann::json entry{
                {"preset", cubey::projects::terrain::terrain_preset_name(preset)},
                {"seed", seed},
                {"min_height_m", summary.min_height_m},
                {"max_height_m", summary.max_height_m},
                {"mean_height_m", summary.mean_height_m},
                {"relief_m", summary.max_height_m - summary.min_height_m},
                {"mean_slope", summary.mean_slope},
            };
            if (version == cubey::projects::terrain::TerrainSourceVersion::V3) {
                const auto components =
                    cubey::projects::terrain::summarize_terrain_source_components(
                        parameters, {0.0F, 0.0F}, kExtentM, kSamplesPerAxis);
                entry["components"] = {
                    {"range_support_mean", components.range_support_mean},
                    {"range_support_coverage", components.range_support_coverage},
                    {"massif_rms_m", components.massif_rms_m},
                    {"massif_max_m", components.massif_max_m},
                    {"valley_rms_m", components.valley_rms_m},
                    {"valley_max_abs_m", components.valley_max_abs_m},
                    {"ridge_rms_m", components.ridge_rms_m},
                    {"ridge_max_m", components.ridge_max_m},
                    {"meso_rms_m", components.meso_rms_m},
                    {"meso_max_abs_m", components.meso_max_abs_m},
                };
            }
            summaries.push_back(std::move(entry));
        }
    }

    const nlohmann::json report{
        {"schema", "cubey.terrain." +
                       std::string(cubey::projects::terrain::terrain_source_version_name(version)) +
                       ".source-summary"},
        {"domain_extent_m", kExtentM},
        {"samples_per_axis", kSamplesPerAxis},
        {"weathering", "off"},
        {"summaries", std::move(summaries)},
    };
    std::cout << report.dump(2) << '\n';
    return 0;
}
