#include "terrain_capture_preparation.h"
#include "terrain_config.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace cubey::projects::terrain {
std::unique_ptr<cubey::TerrainWaterCaptureRuntime>
prepare_terrain_capture(const TerrainProjectConfig& config, const std::filesystem::path& shaders) {
    if (config.captured_water_focus_x_m.has_value() !=
            config.captured_water_focus_z_m.has_value() ||
        config.captured_water_detail_focus_x_m.has_value() !=
            config.captured_water_detail_focus_z_m.has_value())
        throw std::runtime_error("capture preparation requires paired XZ focus values");
    std::unique_ptr<cubey::TerrainWaterCaptureRuntime> runtime;
    if (config.terrain_site && config.water_capture_path)
        runtime = std::make_unique<cubey::TerrainWaterCaptureRuntime>(
            *config.terrain_site, *config.water_capture_path, shaders);
    else if (config.terrain_site)
        runtime =
            std::make_unique<cubey::TerrainWaterCaptureRuntime>(*config.terrain_site, shaders);
    else if (config.water_capture_path)
        runtime = std::make_unique<cubey::TerrainWaterCaptureRuntime>(
            *config.water_capture_path, config.runtime.heightfield_path, shaders);
    else
        throw std::runtime_error("capture preparation requires a resolved site or capture");
    // Validate before GPU staging/old-resident retirement, including uniform
    // mode where fixed topology would not otherwise validate a sticky focus.
    if (config.captured_water_focus_x_m) {
        const auto& grid = runtime->capture().grid;
        const auto sample =
            (cubey::math::Vec2{*config.captured_water_focus_x_m, *config.captured_water_focus_z_m} -
             grid.sample_origin_xz_m) /
            grid.cell_size_m;
        if (!std::isfinite(sample.x) || !std::isfinite(sample.y) || sample.x < 0 || sample.y < 0 ||
            sample.x > float(grid.width - 1U) || sample.y > float(grid.height - 1U))
            throw std::runtime_error("capture camera focus is outside the captured domain");
    }
    runtime->set_terrain_readability(config.terrain_readability == "filtered-study" ? 3U
                                     : config.terrain_readability == "refined"      ? 2U
                                     : config.terrain_readability == "detail-off"   ? 1U
                                                                                    : 0U);
    if (config.captured_water_shadow_strength)
        runtime->set_shadow_strength(*config.captured_water_shadow_strength);
    if (config.captured_water_environment == "shared")
        runtime->enable_environment({config.atmosphere});
    if (config.captured_water_context)
        runtime->prepare_context(
            config.captured_water_context_source.value_or(std::filesystem::path{}),
            config.terrain_context_max_spacing_m);
    const auto focus =
        config.captured_water_detail_focus_x_m
            ? cubey::math::Vec2{*config.captured_water_detail_focus_x_m,
                                *config.captured_water_detail_focus_z_m}
        : config.captured_water_focus_x_m
            ? cubey::math::Vec2{*config.captured_water_focus_x_m, *config.captured_water_focus_z_m}
            : runtime->capture().grid.center_xz_m();
    if (config.captured_water_detail != "uniform")
        runtime->prepare_fixed_lod(focus, config.captured_water_detail_span_m,
                                   config.captured_water_vertex_budget);
    std::printf("terrain_capture_prepared: detail=%s context=%u readability=%s focus=(%.3f,%.3f) "
                "span=%.3f vertex_budget=%u\n",
                config.captured_water_detail.c_str(), config.captured_water_context ? 1U : 0U,
                config.terrain_readability.c_str(), focus.x, focus.y,
                config.captured_water_detail_span_m, config.captured_water_vertex_budget);
    return runtime;
}
} // namespace cubey::projects::terrain
