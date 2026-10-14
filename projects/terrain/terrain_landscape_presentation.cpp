#include "terrain_landscape_presentation.h"
#include "terrain_config.h"
#include <cubey/asset/terrain_water_capture.h>
#include <iomanip>
#include <sstream>

namespace cubey::projects::terrain {
std::string terrain_landscape_selection_label(const cubey::asset::TerrainLandscapePreset& preset) {
    return preset.label + " / " + preset.water_label;
}
TerrainLandscapePresentation
terrain_landscape_presentation(const TerrainProjectConfig& config,
                               const cubey::asset::TerrainWaterCapture& capture) {
    TerrainLandscapePresentation result;
    result.has_water = !config.terrain_site || config.site_water_binding_path.has_value();
    result.title = config.terrain_site ? "Custom terrain site" : "Custom saved landscape";
    if (config.landscape_catalog)
        for (const auto& preset : config.landscape_catalog->presets)
            if (preset.id == config.active_landscape_id) {
                result.title = terrain_landscape_selection_label(preset);
                break;
            }
    result.mode =
        result.has_water ? "Saved water - no solver running" : "Terrain only - no water loaded";
    const auto& grid = capture.grid;
    std::ostringstream water;
    if (result.has_water) {
        water << std::fixed << std::setprecision(2)
              << "Water: " << double(grid.width - 1U) * grid.cell_size_m * .001 << " x "
              << double(grid.height - 1U) * grid.cell_size_m * .001 << " km / "
              << std::setprecision(0) << grid.cell_size_m << " m cells";
        std::ostringstream provenance;
        provenance << std::fixed << std::setprecision(2) << "Saved after "
                   << capture.capture_time_s / 3600.0 << " simulated hours at "
                   << std::setprecision(0) << capture.rain_mm_per_hour << " mm/h rain.";
        result.provenance = provenance.str();
    } else {
        water << "Water: none (terrain-only selection)";
        result.provenance = "No hydraulic capture or rainfall history is loaded.";
    }
    result.water_coverage = water.str();
    std::ostringstream terrain;
    terrain << std::fixed << std::setprecision(2);
    if (config.terrain_site && config.captured_water_context) {
        terrain << "Terrain backdrop: " << config.terrain_site->level_spans_m[3] * .001 << " km";
        if (config.captured_water_detail != "uniform")
            terrain << " / fine region: " << config.captured_water_detail_span_m * .001 << " km";
        else
            terrain << " / uniform detail";
    } else
        terrain << "Terrain: " << double(grid.width - 1U) * grid.cell_size_m * .001 << " x "
                << double(grid.height - 1U) * grid.cell_size_m * .001
                << " km core (wider backdrop off)";
    // Custom capture extensions have no admitted site-span descriptor.
    if (!config.terrain_site && config.captured_water_context)
        terrain.str("Wider terrain extension on; water coverage is unchanged.");
    result.terrain_coverage = terrain.str();
    return result;
}
} // namespace cubey::projects::terrain
