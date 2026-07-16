#include "terrain_source_study.h"

#include "terrain_app.h"
#include "terrain_config.h"

#include <cubey/core/run_config.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace {

int run_source_study(cubey::RunConfig config) {
    if (!config.terrain.render_path.empty() && config.terrain.render_path != "backdrop") {
        throw std::runtime_error("terrain source study supports only the backdrop render path");
    }
    if (!config.terrain.backdrop_mesh_density.empty() &&
        config.terrain.backdrop_mesh_density != "high") {
        throw std::runtime_error("terrain source study requires high backdrop mesh density");
    }
    if (!config.terrain.camera_preset.empty() && config.terrain.camera_preset != "backdrop" &&
        config.terrain.camera_preset != "backdrop-stage") {
        throw std::runtime_error("terrain source study requires a backdrop camera preset");
    }
    if (!config.terrain.weathering.empty() && config.terrain.weathering != "off") {
        throw std::runtime_error("terrain source study disables weathering");
    }

    const auto recipe =
        cubey::projects::terrain::terrain_source_study_recipe_from_name(config.terrain.recipe);
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

    const cubey::projects::terrain::TerrainRuntimeConfig runtime =
        cubey::projects::terrain::terrain_runtime_config_from_run_config(config);
    const cubey::projects::terrain::TerrainSourceStudySource source(recipe, runtime.source.seed);
    return cubey::projects::terrain::run_terrain_with_options(config,
                                                              {
                                                                  .backdrop_source = &source,
                                                                  .backdrop_render_stride = 1U,
                                                              });
}

} // namespace

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "terrain_source_study",
                                  .default_title = "cubey terrain source study",
                              },
                              run_source_study);
}
