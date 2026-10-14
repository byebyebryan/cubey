#include "terrain_config.h"
#include "terrain_landscape_presentation.h"
#include <cubey/asset/terrain_water_capture.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
} // namespace
int main() {
    try {
        using namespace cubey::projects::terrain;
        TerrainProjectConfig config;
        config.terrain_site.emplace();
        config.terrain_site->level_spans_m = {16384, 32768, 65536, 131072};
        config.site_water_binding_path = "binding.json";
        config.captured_water_context = true;
        config.captured_water_detail = "fixed";
        config.captured_water_detail_span_m = 16384;
        config.active_landscape_id = "mountain-local";
        config.landscape_catalog.emplace();
        cubey::asset::TerrainLandscapePreset preset;
        preset.id = "mountain-local";
        preset.label = "Original mountain";
        preset.water_label = "Sharper water";
        config.landscape_catalog->presets.push_back(preset);
        cubey::asset::TerrainWaterCapture capture;
        capture.grid = {2048, 2048, 32, {-21232, -13552}};
        capture.capture_time_s = 43200;
        capture.rain_mm_per_hour = 512;
        const auto wet = terrain_landscape_presentation(config, capture);
        require(wet.title == "Original mountain / Sharper water", "raw ID exposed as title");
        require(wet.has_water && wet.mode == "Saved water - no solver running", "wrong wet mode");
        require(wet.water_coverage == "Water: 65.50 x 65.50 km / 32 m cells",
                "wrong sampled water span");
        require(wet.terrain_coverage == "Terrain backdrop: 131.07 km / fine region: 16.38 km",
                "coverage conflated");
        require(wet.provenance == "Saved after 12.00 simulated hours at 512 mm/h rain.",
                "capture history incorrect");
        config.captured_water_detail = "uniform";
        require(terrain_landscape_presentation(config, capture)
                        .terrain_coverage.find("uniform detail") != std::string::npos,
                "uniform mode implies fixed fine region");
        config.captured_water_context = false;
        require(
            terrain_landscape_presentation(config, capture).terrain_coverage.find("backdrop off") !=
                std::string::npos,
            "context-off still claims wide ground");
        config.site_water_binding_path.reset();
        const auto dry = terrain_landscape_presentation(config, capture);
        require(!dry.has_water && dry.mode == "Terrain only - no water loaded",
                "dry terrain implies saved water");
        require(dry.provenance.find("No hydraulic capture") == 0 &&
                    dry.water_coverage.find("none") != std::string::npos,
                "dry terrain implies rain");
        config.terrain_site.reset();
        config.landscape_catalog.reset();
        config.captured_water_context = true;
        const auto custom = terrain_landscape_presentation(config, capture);
        require(custom.has_water && custom.title == "Custom saved landscape",
                "custom capture mislabeled");
        require(custom.terrain_coverage.find("water coverage is unchanged") != std::string::npos,
                "custom extension claims water");
        capture.grid.height = 1024;
        require(
            terrain_landscape_presentation(config, capture).water_coverage.find("65.50 x 32.74") !=
                std::string::npos,
            "rectangular capture mislabeled");
        std::cout << "terrain_landscape_presentation: PASS names, modes, sampled coverage and "
                     "provenance\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
