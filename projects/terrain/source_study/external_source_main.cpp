#include "terrain_raster_height_source.h"

#include "terrain_app.h"
#include "terrain_backdrop_stage.h"
#include "terrain_config.h"

#include <cubey/core/run_config.h>

#include <algorithm>
#include <stdexcept>

namespace {

constexpr float kOuterRadiusM = 16'384.0F;

int run_external_source_study(cubey::RunConfig config) {
    using namespace cubey::projects::terrain;
    if (config.terrain.study_field_path.empty()) {
        throw std::runtime_error("terrain external source study requires --terrain-study-field");
    }
    if (!config.terrain.render_path.empty() && config.terrain.render_path != "backdrop") {
        throw std::runtime_error("terrain external source study supports only the backdrop path");
    }
    if (!config.terrain.backdrop_mesh_density.empty() &&
        config.terrain.backdrop_mesh_density != "high") {
        throw std::runtime_error("terrain external source study requires high mesh density");
    }
    if (!config.terrain.weathering.empty() && config.terrain.weathering != "off") {
        throw std::runtime_error("terrain external source study disables weathering");
    }

    const std::filesystem::path field_path = config.terrain.study_field_path;
    config.terrain.study_field_path.clear();
    TerrainRasterHeightSource source(field_path);
    if (config.terrain.seed_set && config.terrain.seed != source.metadata().seed) {
        throw std::runtime_error("terrain seed does not match raster field manifest");
    }
    config.terrain.seed = source.metadata().seed;
    config.terrain.seed_set = true;
    config.terrain.render_path = "backdrop";
    config.terrain.backdrop_profile = "hard-cut-v1";
    config.terrain.backdrop_mesh_density = "high";
    config.terrain.camera_preset =
        config.terrain.camera_preset.empty() ? "backdrop" : config.terrain.camera_preset;
    config.terrain.preset = "mountain";
    config.terrain.source_version = "v2.1";
    config.terrain.weathering = "off";
    config.terrain.presentation =
        config.terrain.presentation.empty() ? "backdrop" : config.terrain.presentation;

    const TerrainRuntimeConfig runtime = terrain_runtime_config_from_run_config(config);
    TerrainBackdropStageRequest request = terrain_backdrop_stage_request(
        runtime.backdrop_mode, static_cast<float>(config.width) / static_cast<float>(config.height),
        runtime.vertical_scale);
    request.minimum_visible_terrain_distance_m = runtime.backdrop_minimum_visible_distance_m;
    request.search_extent_m = 12'000.0F;
    request.search_step_m = 4'000.0F;
    if (runtime.backdrop_orbit_radius_m.has_value()) {
        request.orbit_default_radius_m = runtime.backdrop_orbit_radius_m.value();
    }
    if (runtime.backdrop_elevation_radians.has_value()) {
        request.orbit_default_elevation_radians = runtime.backdrop_elevation_radians.value();
    }
    const TerrainBackdropStagePlan stage = plan_terrain_backdrop_stage(source, request);
    if (!source.contains_disk(stage.source_focus_xz,
                              kOuterRadiusM + source.metadata().gradient_step_m)) {
        throw std::runtime_error("terrain raster field does not cover the selected backdrop");
    }
    return run_terrain_with_options(config,
                                    {
                                        .backdrop_source = &source,
                                        .backdrop_render_stride = 1U,
                                        .backdrop_center_mode = TerrainBackdropCenterMode::Cutout,
                                        .backdrop_stage_plan = stage,
                                        .backdrop_outer_radius_m = kOuterRadiusM,
                                    });
}

} // namespace

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "terrain_external_source_study",
                                  .default_title = "cubey terrain external source study",
                              },
                              run_external_source_study);
}
